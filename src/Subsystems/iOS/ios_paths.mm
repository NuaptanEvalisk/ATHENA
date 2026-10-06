/******************************************************************************
* MODULE     : ios_paths.mm
* DESCRIPTION: Resolve immutable bundle resources and local iPad app storage
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "athena_ios.hpp"
#import <Foundation/Foundation.h>
#include <cstdlib>
#include <unistd.h>

bool athena_ios_initialize_paths (std::string& error) {
  @autoreleasepool {
    NSFileManager* files= NSFileManager.defaultManager;
    NSError* failure= nil;
    NSURL* resources= [NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"ATHENA" isDirectory:YES];
    if (![files fileExistsAtPath:[[resources URLByAppendingPathComponent:@"progs/init-athena.scm"] path]]) {
      error= "ATHENA resources are missing from the application bundle";
      return false;
    }
    NSURL* support= [files URLForDirectory:NSApplicationSupportDirectory inDomain:NSUserDomainMask
                        appropriateForURL:nil create:YES error:&failure];
    NSURL* documents= [files URLForDirectory:NSDocumentDirectory inDomain:NSUserDomainMask
                           appropriateForURL:nil create:YES error:&failure];
    if (!support || !documents) {
      error= failure.localizedDescription.UTF8String;
      return false;
    }
    support= [support URLByAppendingPathComponent:@"ATHENA" isDirectory:YES];
    NSURL* vaults= [documents URLByAppendingPathComponent:@"Vaults" isDirectory:YES];
    for (NSURL* directory in @[support, vaults]) {
      if (![files createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:&failure]) {
        error= failure.localizedDescription.UTF8String;
        return false;
      }
    }
    // Only reconstructible caches are excluded from device backups. Settings,
    // recovery documents and local vaults remain durable application data.
    for (NSString* suffix in @[@"system/cache", @"system/tmp"]) {
      NSURL* cache= [support URLByAppendingPathComponent:suffix isDirectory:YES];
      if (![files createDirectoryAtURL:cache withIntermediateDirectories:YES attributes:nil error:&failure] ||
          ![cache setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&failure]) {
        error= failure.localizedDescription.UTF8String;
        return false;
      }
    }
    setenv ("ATHENA_PATH", resources.path.UTF8String, 1);
    setenv ("ATHENA_HOME_PATH", support.path.UTF8String, 1);
    setenv ("ATHENA_GUILE_RUNTIME_ROOT",
      [resources URLByAppendingPathComponent:@"lib/athena-guile"].path.UTF8String, 1);
    setenv ("ATHENA_VAULTS_PATH", vaults.path.UTF8String, 1);
    setenv ("PWD", documents.path.UTF8String, 1);
    if (chdir (documents.path.UTF8String) != 0) {
      error= "Could not enter local Documents directory";
      return false;
    }
    return true;
  }
}
