/******************************************************************************
* MODULE     : identity.cpp
* DESCRIPTION: Persistent CURVE identities and atomic client authorization storage
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "identity.hpp"
#include <zmq.h>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace athena::interop {
namespace {
struct descriptor {
  int fd;
  explicit descriptor (int fd): fd (fd) { if (fd < 0) throw std::runtime_error ("Cannot open private AUDMAP file"); }
  ~descriptor () { ::close (fd); }
};
void check_private (int fd) {
  struct stat st {};
  if (fstat (fd, &st) || !S_ISREG (st.st_mode) || st.st_uid != getuid () || (st.st_mode & 077))
    throw std::runtime_error ("AUDMAP private files must be owned by this user with mode 0600");
}

// Serialize read-modify-write across ATHENA instances, and atomically publish
// complete files. The lock file is never replaced along with the data file.
class private_file {
  std::filesystem::path path;
  std::unique_ptr<descriptor> lock;
public:
  explicit private_file (const std::filesystem::path& p): path (std::filesystem::absolute (p)) {
    const auto directory = path.parent_path ();
    std::filesystem::create_directories (directory.parent_path ());
    if (::mkdir (directory.c_str (), 0700) && errno != EEXIST)
      throw std::runtime_error ("Cannot create AUDMAP private directory");
    struct stat st {};
    if (lstat (directory.c_str (), &st) || !S_ISDIR (st.st_mode) || st.st_uid != getuid () || (st.st_mode & 077))
      throw std::runtime_error ("AUDMAP identity/policy directory must be private (0700)");
    lock = std::make_unique<descriptor> (::open ((path.string () + ".lock").c_str (),
      O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600));
    check_private (lock->fd);
    if (flock (lock->fd, LOCK_EX)) throw std::runtime_error ("Cannot lock AUDMAP private file");
  }
  value read () const {
    int fd = ::open (path.c_str (), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno == ENOENT) return nullptr;
    descriptor input (fd); check_private (fd);
    std::string data;
    char block[4096];
    for (;;) {
      const auto n = ::read (fd, block, sizeof (block));
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) throw std::runtime_error ("Cannot read AUDMAP private file");
      if (!n) break;
      data.append (block, n);
      if (data.size () > 1024 * 1024) throw std::runtime_error ("AUDMAP private file too large");
    }
    return value::parse (data);
  }
  void write (const value& data) const {
    const auto bytes = data.dump (2);
    if (bytes.size () > 1024 * 1024) throw std::runtime_error ("AUDMAP private file too large");
    std::string name = path.string () + ".XXXXXX";
    descriptor output (::mkstemp (name.data ()));
    try {
      fcntl (output.fd, F_SETFD, FD_CLOEXEC);
      std::size_t offset = 0;
      while (offset < bytes.size ()) {
        const auto n = ::write (output.fd, bytes.data () + offset, bytes.size () - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error ("Cannot write AUDMAP private file");
        offset += n;
      }
      if (fsync (output.fd) || ::rename (name.c_str (), path.c_str ()))
        throw std::runtime_error ("Cannot publish AUDMAP private file");
      descriptor directory (::open (path.parent_path ().c_str (), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
      if (fsync (directory.fd)) throw std::runtime_error ("Cannot sync AUDMAP private directory");
    }
    catch (...) { ::unlink (name.c_str ()); throw; }
  }
};
void validate_key (const std::string& key) {
  std::uint8_t bytes[32];
  if (key.size () != 40 || key.find ('\0') != std::string::npos || !zmq_z85_decode (bytes, key.c_str ()))
    throw std::invalid_argument ("Invalid CURVE public key");
}
} // namespace

client_identity load_client_identity (const std::filesystem::path& path) {
  private_file file (path);
  auto data = file.read ();
  if (data.is_null ()) {
    char pub[41], secret[41];
    if (zmq_curve_keypair (pub, secret)) throw std::runtime_error ("CURVE key generation failed");
    data = {{"version", 1}, {"public_key", pub}, {"secret_key", secret}};
    file.write (data);
  }
  if (data.at ("version") != 1) throw std::runtime_error ("Unknown AUDMAP identity version");
  client_identity identity {data.at ("public_key"), data.at ("secret_key")};
  validate_key (identity.public_key); validate_key (identity.secret_key);
  char derived[41];
  if (zmq_curve_public (derived, identity.secret_key.c_str ()) || identity.public_key != derived)
    throw std::runtime_error ("AUDMAP identity key pair does not match");
  return identity;
}

std::filesystem::path default_client_identity (const std::string& profile) {
  const char* config = std::getenv ("XDG_CONFIG_HOME");
  const char* home = std::getenv ("HOME");
  if ((!config || !*config) && (!home || !*home)) throw std::runtime_error ("No client configuration directory");
  auto base = config && *config ? std::filesystem::path (config) : std::filesystem::path (home) / ".config";
  return base / "athena-audmap" / (profile + ".json");
}

std::optional<remembered_authorization> authorization_store::lookup (const std::string& key) const {
  validate_key (key);
  auto data = private_file (path).read ();
  if (data.is_null ()) return {};
  if (data.at ("version") != 1) throw std::runtime_error ("Unknown AUDMAP authorization version");
  const auto& clients = data.at ("clients");
  if (!clients.is_object ()) throw std::runtime_error ("Invalid AUDMAP authorization store");
  if (!clients.contains (key)) return {};
  const auto& rule = clients.at (key);
  const int mode = rule.at ("trust").get<int> ();
  if (mode < 0 || mode > 2) throw std::runtime_error ("Invalid stored trust mode");
  return remembered_authorization {rule.at ("allow").get<bool> (), static_cast<trust_mode> (mode)};
}

void authorization_store::remember (const std::string& key, remembered_authorization rule) const {
  validate_key (key);
  private_file file (path);
  auto data = file.read ();
  if (data.is_null ()) data = {{"version", 1}, {"clients", value::object ()}};
  if (data.at ("version") != 1 || !data.at ("clients").is_object ())
    throw std::runtime_error ("Invalid AUDMAP authorization store");
  data["clients"][key] = {{"allow", rule.allow}, {"trust", static_cast<int> (rule.trust)}};
  file.write (data);
}
} // namespace athena::interop
