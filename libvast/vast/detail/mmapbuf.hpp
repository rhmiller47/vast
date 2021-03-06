#ifndef VAST_DETAIL_MMAPBUF_HPP
#define VAST_DETAIL_MMAPBUF_HPP

#include <cstddef>
#include <streambuf>
#include <string>

namespace vast {
namespace detail {

// TODO: add support for writing to the buffer via the put area.

/// A memory-mapped streambuffer. The put and get areas corresponds to the
/// mapped memory region.
class mmapbuf : public std::streambuf {
public:
  /// Constructs a memory-mapped streambuffer from a file.
  /// @param filename The path to the file to open.
  /// @param size The size of the file in bytes. If 0, figure out file size
  ///             automatically.
  /// @param offset An offset where to begin mapping.
  /// @pre `offset < size`
  explicit mmapbuf(const std::string& filename, size_t size = 0);

  /// Closes the opened file and unmaps the mapped memory region.
  ~mmapbuf();

  /// Returns the size of the mapped memory region.
  size_t size() const;

protected:
  std::streamsize showmanyc() override;

  int_type underflow() override;

  std::streamsize xsgetn(char_type* s, std::streamsize n) override;

  pos_type seekoff(off_type off,
                   std::ios_base::seekdir dir,
                   std::ios_base::openmode which = std::ios_base::in) override;

  pos_type seekpos(pos_type pos,
                   std::ios_base::openmode which = std::ios_base::in) override;

private:
  size_t size_;
  int fd_ = -1;
  char_type* map_ = nullptr;
};

} // namespace detail
} // namespace vast

#endif
