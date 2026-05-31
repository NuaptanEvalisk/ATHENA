#include <algorithm>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "convert.hpp"
#include "file.hpp"
#include "tree.hpp"
#include "url.hpp"
#include "vault.hpp"
#include "tm_ostream.hpp"
#include "aofm_telemetry.hpp"
#include "aofm_import_vault_internal.hpp"

#if defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace aofm_import_vault_internal;

namespace {

enum class IpcMsgType { PROGRESS, ERROR_MSG, DONE };

struct IpcTelemetryData {
  double time_parse_latex_doc;
  double time_latex_to_tree;
  double aofm_math_time;
  int aofm_math_count;
  double time_l2t_kill_space;
  double time_l2t_parsed_latex;
  double time_l2t_finalize_doc;
  double time_l2t_handle_matches;
  double time_l2t_upgrade_tex;
  double time_l2t_finalize_misc;
  double time_l2t_drd_correct;
  double time_l2t_style_check;
  double time_l2t_simplify_correct;
  double time_l2t_latex_correct;
  double time_l2t_guess_missing;
  double time_l2t_post_metadata;
  int count_l2t_is_document;
  int count_l2t_total;
};

struct IpcMessage {
  IpcMsgType type;
  union {
    char filename[256];
    IpcTelemetryData telemetry;
  };
};

} // namespace

void
print_progress_bar(size_t current, size_t total, const std::string& filename) {
  int bar_width = 30;
  float progress = (float)current / (float)total;
  int pos = (int)(bar_width * progress);

  std::cout << "\r[" ;
  for (int i = 0; i < bar_width; ++i) {
    if (i < pos) std::cout << "=";
    else if (i == pos) std::cout << ">";
    else std::cout << " ";
  }

  std::string display_path = filename;
  if (display_path.length() > 40) {
    display_path = "..." + display_path.substr(display_path.length() - 37);
  }

  std::cout << "] " << (int)(progress * 100.0) << "% "
            << "[" << current << "/" << total << "] "
            << "Converting: " << display_path << "                             " << std::flush;
}

bool
aofm_import_vault(string source_dir, string destination_dir,
                  bool ignore_nonempty, int parallelism,
                  string model_vault) {
  time_parse_latex_doc = 0.0;
  time_latex_to_tree = 0.0;
  aofm_math_time = 0.0;
  aofm_math_count = 0;
  
  time_l2t_kill_space = 0.0;
  time_l2t_parsed_latex = 0.0;
  time_l2t_finalize_doc = 0.0;
  time_l2t_handle_matches = 0.0;
  time_l2t_upgrade_tex = 0.0;
  time_l2t_finalize_misc = 0.0;
  time_l2t_drd_correct = 0.0;
  time_l2t_style_check = 0.0;
  time_l2t_simplify_correct = 0.0;
  time_l2t_latex_correct = 0.0;
  time_l2t_guess_missing = 0.0;
  time_l2t_post_metadata = 0.0;
  
  count_l2t_is_document = 0;
  count_l2t_total = 0;

  url source_root = url_system(source_dir);
  url destination_root = url_system(destination_dir);
  if (!is_rooted(source_root)) {
    source_root = resolve(url_pwd(), "") * source_root;
  }
  if (!is_rooted(destination_root)) {
    destination_root = resolve(url_pwd(), "") * destination_root;
  }

  if (!exists(source_root) || !is_directory(source_root)) {
    report_import_error("source path is not a directory");
    return false;
  }
  if (!validate_destination_dir(destination_root, ignore_nonempty)) return false;

  std::string destination_root_path =
      tm_to_std_string(as_unix_string(destination_root));
  std::string vault_name =
      path_stem_without_trailing_separators(tm_to_std_string(as_unix_string(source_root)));
  if (vault_name.empty()) vault_name = "Vault";

  AofmModelVaultInfo model;
  if (!load_model_vault_info(tm_to_std_string(model_vault),
                             destination_root_path, model))
    return false;

  if (!write_vaultfile(destination_root_path, vault_name,
                       model.active ? model.prefs_rel : "",
                       model.active ? model.namespace_db_rel : "ns.sqlite")) {
    report_import_error("failed to write Vaultfile");
    return false;
  }

  std::vector<ImportFileInfo> files;
  AssetIndexMap asset_map;
  DirChildrenMap dir_children;
  if (!scan_markdown_files(source_root, source_root, destination_root, files,
                           asset_map, dir_children)) {
    report_import_error("failed to scan source vault");
    return false;
  }

  AnchorMap anchor_map;
  AnchorOccurrenceMap anchor_occurrences;
  FileIndexMap file_map;
  HeadingMap heading_map;
  HeadingOccurrenceMap heading_occurrences;
  for (const ImportFileInfo& file_info : files) {
    std::string stem = path_stem(file_info.relative_md_path);
    AofmVaultFileInfo f_info;
    f_info.uuid = tm_to_std_string(vault_generate_uuid());
    f_info.relative_ath_path = file_info.relative_ath_path;
    f_info.stem = stem;
    file_map[stem] = f_info;

    process_markdown_file(file_info, anchor_map, anchor_occurrences,
                          heading_occurrences, heading_map);
  }

  std::string dump_path = join_unix_paths(destination_root_path, "anchor_map.txt");
  std::ofstream dump_file(dump_path);
  if (dump_file.is_open()) {
    dump_anchor_map(anchor_map, dump_file);
    dump_file.close();
  }

  std::string heading_dump_path = join_unix_paths(destination_root_path, "heading_map.txt");
  std::ofstream heading_dump_file(heading_dump_path);
  if (heading_dump_file.is_open()) {
    dump_heading_map(heading_map, heading_dump_file);
    heading_dump_file.close();
  }

  cout << "Starting vault conversion of " << (int) files.size()
       << " files..." << LF;

  size_t total_files = files.size();
  size_t current_index = 0;

#if defined(__unix__) || defined(__APPLE__)
  int num_workers = parallelism > 0 ?
      parallelism : (int) std::thread::hardware_concurrency();
  if (num_workers <= 0) num_workers = 1;
  if (num_workers > (int)files.size()) num_workers = (int)files.size();

  std::vector<int> pipes(num_workers);
  std::vector<pid_t> pids;

  for (int i = 0; i < num_workers; ++i) {
    int fd[2];
    if (pipe(fd) == -1) {
      report_import_error("failed to create pipe");
      return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
      report_import_error("failed to fork");
      return false;
    }

    if (pid == 0) {
      // Child process
      close(fd[0]);
      size_t start = (files.size() * i) / num_workers;
      size_t end = (files.size() * (i + 1)) / num_workers;

      // Reset local telemetry for the child to avoid double-counting inherited values
      time_parse_latex_doc = 0.0;
      time_latex_to_tree = 0.0;
      aofm_math_time = 0.0;
      aofm_math_count = 0;
      time_l2t_kill_space = 0.0;
      time_l2t_parsed_latex = 0.0;
      time_l2t_finalize_doc = 0.0;
      time_l2t_handle_matches = 0.0;
      time_l2t_upgrade_tex = 0.0;
      time_l2t_finalize_misc = 0.0;
      time_l2t_drd_correct = 0.0;
      time_l2t_style_check = 0.0;
      time_l2t_simplify_correct = 0.0;
      time_l2t_latex_correct = 0.0;
      time_l2t_guess_missing = 0.0;
      time_l2t_post_metadata = 0.0;
      count_l2t_is_document = 0;
      count_l2t_total = 0;

      for (size_t j = start; j < end; ++j) {
        const ImportFileInfo& file_info = files[j];
        tree document;
        if (!aofm_convert_tree(as_unix_string(file_info.source_url), document, false)) {
          IpcMessage msg;
          msg.type = IpcMsgType::ERROR_MSG;
          strncpy(msg.filename, file_info.relative_md_path.c_str(), 255);
          msg.filename[255] = '\0';
          write(fd[1], &msg, sizeof(IpcMessage));
          _exit(1);
        }

        tree resolved = resolve_anchor_placeholders(document, anchor_map,
                                                    anchor_occurrences, file_map,
                                                    heading_map,
                                                    heading_occurrences,
                                                    asset_map, dir_children,
                                                    file_info.relative_ath_path);
        resolved = apply_model_namespace_style(resolved, model, file_info);
        string serialized = tree_to_texmacs(resolved);
        std::string destination_path = join_unix_paths(destination_root_path, file_info.relative_ath_path);
        if (save_string(url_system(aofm::std_to_tm_string(destination_path)), serialized)) {
          IpcMessage msg;
          msg.type = IpcMsgType::ERROR_MSG;
          std::string err = "write fail: " + file_info.relative_ath_path;
          strncpy(msg.filename, err.c_str(), 255);
          msg.filename[255] = '\0';
          write(fd[1], &msg, sizeof(IpcMessage));
          _exit(1);
        }

        IpcMessage msg;
        msg.type = IpcMsgType::PROGRESS;
        strncpy(msg.filename, file_info.relative_md_path.c_str(), 255);
        msg.filename[255] = '\0';
        write(fd[1], &msg, sizeof(IpcMessage));
      }

      IpcMessage msg;
      msg.type = IpcMsgType::DONE;
      msg.telemetry.time_parse_latex_doc = time_parse_latex_doc;
      msg.telemetry.time_latex_to_tree = time_latex_to_tree;
      msg.telemetry.aofm_math_time = aofm_math_time;
      msg.telemetry.aofm_math_count = aofm_math_count;
      msg.telemetry.time_l2t_kill_space = time_l2t_kill_space;
      msg.telemetry.time_l2t_parsed_latex = time_l2t_parsed_latex;
      msg.telemetry.time_l2t_finalize_doc = time_l2t_finalize_doc;
      msg.telemetry.time_l2t_handle_matches = time_l2t_handle_matches;
      msg.telemetry.time_l2t_upgrade_tex = time_l2t_upgrade_tex;
      msg.telemetry.time_l2t_finalize_misc = time_l2t_finalize_misc;
      msg.telemetry.time_l2t_drd_correct = time_l2t_drd_correct;
      msg.telemetry.time_l2t_style_check = time_l2t_style_check;
      msg.telemetry.time_l2t_simplify_correct = time_l2t_simplify_correct;
      msg.telemetry.time_l2t_latex_correct = time_l2t_latex_correct;
      msg.telemetry.time_l2t_guess_missing = time_l2t_guess_missing;
      msg.telemetry.time_l2t_post_metadata = time_l2t_post_metadata;
      msg.telemetry.count_l2t_is_document = count_l2t_is_document;
      msg.telemetry.count_l2t_total = count_l2t_total;
      write(fd[1], &msg, sizeof(IpcMessage));
      _exit(0);
    } else {
      // Parent process
      close(fd[1]);
      pipes[i] = fd[0];
      pids.push_back(pid);
    }
  }

  // Parent monitoring loop
  std::vector<pollfd> poll_fds(num_workers);
  for (int i = 0; i < num_workers; ++i) {
    poll_fds[i].fd = pipes[i];
    poll_fds[i].events = POLLIN;
  }

  int active_workers = num_workers;
  while (active_workers > 0) {
    int ret = poll(poll_fds.data(), num_workers, -1);
    if (ret <= 0) continue;

    for (int i = 0; i < num_workers; ++i) {
      if (poll_fds[i].fd != -1 && (poll_fds[i].revents & POLLIN)) {
        IpcMessage msg;
        ssize_t n = read(poll_fds[i].fd, &msg, sizeof(IpcMessage));
        if (n <= 0) {
          if (n == 0) {
            close(poll_fds[i].fd);
            poll_fds[i].fd = -1;
            active_workers--;
          }
          continue;
        }

        if (msg.type == IpcMsgType::PROGRESS) {
          current_index++;
          print_progress_bar(current_index, total_files, msg.filename);
        } else if (msg.type == IpcMsgType::ERROR_MSG) {
          std::cout << std::endl;
          report_import_error("child error: " + std::string(msg.filename));
          // For simplicity, we abort on first child error
          for (pid_t p : pids) kill(p, SIGTERM);
          return false;
        } else if (msg.type == IpcMsgType::DONE) {
          time_parse_latex_doc += msg.telemetry.time_parse_latex_doc;
          time_latex_to_tree += msg.telemetry.time_latex_to_tree;
          aofm_math_time += msg.telemetry.aofm_math_time;
          aofm_math_count += msg.telemetry.aofm_math_count;
          time_l2t_kill_space += msg.telemetry.time_l2t_kill_space;
          time_l2t_parsed_latex += msg.telemetry.time_l2t_parsed_latex;
          time_l2t_finalize_doc += msg.telemetry.time_l2t_finalize_doc;
          time_l2t_handle_matches += msg.telemetry.time_l2t_handle_matches;
          time_l2t_upgrade_tex += msg.telemetry.time_l2t_upgrade_tex;
          time_l2t_finalize_misc += msg.telemetry.time_l2t_finalize_misc;
          time_l2t_drd_correct += msg.telemetry.time_l2t_drd_correct;
          time_l2t_style_check += msg.telemetry.time_l2t_style_check;
          time_l2t_simplify_correct += msg.telemetry.time_l2t_simplify_correct;
          time_l2t_latex_correct += msg.telemetry.time_l2t_latex_correct;
          time_l2t_guess_missing += msg.telemetry.time_l2t_guess_missing;
          time_l2t_post_metadata += msg.telemetry.time_l2t_post_metadata;
          count_l2t_is_document += msg.telemetry.count_l2t_is_document;
          count_l2t_total += msg.telemetry.count_l2t_total;
        }
      } else if (poll_fds[i].fd != -1 && (poll_fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))) {
        close(poll_fds[i].fd);
        poll_fds[i].fd = -1;
        active_workers--;
      }
    }
  }

  for (pid_t pid : pids) {
    waitpid(pid, NULL, 0);
  }
#else
  for (const ImportFileInfo& file_info : files) {
    current_index++;
    print_progress_bar(current_index, total_files, file_info.relative_md_path);

    tree document;
    if (!aofm_convert_tree(as_unix_string(file_info.source_url), document, false)) {
      std::cout << std::endl;
      report_import_error("failed to convert file: " + file_info.relative_md_path);
      return false;
    }

    tree resolved =
        resolve_anchor_placeholders(document, anchor_map, anchor_occurrences,
                                    file_map, heading_map, heading_occurrences,
                                    asset_map, dir_children,
                                    file_info.relative_ath_path);
    resolved = apply_model_namespace_style(resolved, model, file_info);
    string serialized = tree_to_texmacs(resolved);
    std::string destination_path =
        join_unix_paths(destination_root_path, file_info.relative_ath_path);
    if (save_string(url_system(aofm::std_to_tm_string(destination_path)), serialized)) {
      std::cout << std::endl;
      report_import_error("failed to write destination file: " + destination_path);
      return false;
    }
  }
#endif

  if (!write_vault_database(destination_root, file_map, anchor_map, heading_map)) {
    std::cout << std::endl;
    report_import_error("failed to write vault database map.tmdb");
    return false;
  }

  cout << "\nVault conversion completed successfully." << LF;

  if (aofm_math_count > 0) {
    cout << "AOFM] Total math processing: " << aofm_math_time << "s ("
         << aofm_math_count << " formulas, avg "
         << (aofm_math_time / aofm_math_count) << "s)" << LF;
  }
  cout << "AOFM] Total parse_latex_document: " << time_parse_latex_doc
       << "s" << LF;
  cout << "AOFM] Total latex_to_tree: " << time_latex_to_tree << "s" << LF;
  cout << "  - kill_space_invaders: " << time_l2t_kill_space << "s" << LF;
  cout << "  - parsed_latex_to_tree: " << time_l2t_parsed_latex << "s" << LF;
  cout << "  - finalize_doc/preamble: " << time_l2t_finalize_doc << "s" << LF;
  cout << "  - handle_matches: " << time_l2t_handle_matches << "s" << LF;
  cout << "  - upgrade_tex: " << time_l2t_upgrade_tex << "s" << LF;
  cout << "  - finalize_misc/textm: " << time_l2t_finalize_misc << "s" << LF;
  cout << "  - drd_correct: " << time_l2t_drd_correct << "s" << LF;
  cout << "  - style_check (exists): " << time_l2t_style_check << "s" << LF;
  cout << "  - simplify_correct: " << time_l2t_simplify_correct << "s" << LF;
  cout << "  - latex_correct: " << time_l2t_latex_correct << "s" << LF;
  cout << "  - guess_missing: " << time_l2t_guess_missing << "s" << LF;
  cout << "  - postprocess_metadata: " << time_l2t_post_metadata << "s" << LF;
  cout << "AOFM] is_document counts: " << count_l2t_is_document << " / "
       << count_l2t_total << LF;

  return true;
}
