#include "ZipReader.h"

#include <windows.h>

#include "miniz.h"

namespace apk {

static size_t fileReadCallback(void *opaque, mz_uint64 ofs, void *buf, size_t n)
{
    HANDLE h = static_cast<HANDLE>(opaque);
    if (h == INVALID_HANDLE_VALUE || !h) return 0;
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)ofs;
    if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return 0;
    size_t total = 0;
    while (total < n) {
        size_t remain = n - total;
        DWORD want = (remain > 0x1000000u) ? 0x1000000u : (DWORD)remain;
        DWORD got = 0;
        if (!ReadFile(h, (uint8_t *)buf + total, want, &got, nullptr)) break;
        if (got == 0) break;
        total += got;
    }
    return total;
}

struct ZipReader::Impl {
    HANDLE file = INVALID_HANDLE_VALUE;
    mz_zip_archive zip;
    bool zipInited = false;
    ~Impl() { }
};

ZipReader::ZipReader()
{
    d_ = new Impl();
    memset(&d_->zip, 0, sizeof(d_->zip));
}

ZipReader::~ZipReader()
{
    close();
    delete d_;
}

bool ZipReader::open(const std::wstring &path)
{
    close();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) {
        CloseHandle(h);
        return false;
    }

    d_->file = h;
    memset(&d_->zip, 0, sizeof(d_->zip));
    d_->zip.m_pRead = fileReadCallback;
    d_->zip.m_pIO_opaque = h;

    if (!mz_zip_reader_init(&d_->zip, (mz_uint64)sz.QuadPart, 0)) {
        CloseHandle(h);
        d_->file = INVALID_HANDLE_VALUE;
        return false;
    }
    d_->zipInited = true;
    open_ = true;
    return true;
}

void ZipReader::close()
{
    if (!d_) return;
    if (d_->zipInited) {
        mz_zip_reader_end(&d_->zip);
        d_->zipInited = false;
    }
    if (d_->file != INVALID_HANDLE_VALUE) {
        CloseHandle(d_->file);
        d_->file = INVALID_HANDLE_VALUE;
    }
    open_ = false;
}

uint32_t ZipReader::entryCount() const
{
    if (!open_ || !d_->zipInited) return 0;
    return (uint32_t)mz_zip_reader_get_num_files(&d_->zip);
}

bool ZipReader::entryNameAt(uint32_t index, std::string *out) const
{
    if (!open_ || !d_->zipInited) return false;
    if (index >= entryCount()) return false;
    mz_zip_archive_file_stat st;
    memset(&st, 0, sizeof(st));
    if (!mz_zip_reader_file_stat(&d_->zip, (mz_uint)index, &st)) return false;
    if (out) *out = std::string(st.m_filename);
    return true;
}

std::vector<std::string> ZipReader::listEntries() const
{
    std::vector<std::string> out;
    const uint32_t n = entryCount();
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        std::string name;
        if (entryNameAt(i, &name)) out.push_back(name);
    }
    return out;
}

bool ZipReader::readEntryAt(uint32_t index, std::vector<uint8_t> *out) const
{
    if (!open_ || !d_->zipInited || !out) return false;
    if (index >= entryCount()) return false;

    mz_zip_archive_file_stat st;
    memset(&st, 0, sizeof(st));
    if (!mz_zip_reader_file_stat(&d_->zip, (mz_uint)index, &st)) return false;
    if (st.m_is_directory) return false;
    if (st.m_uncomp_size == 0) { out->clear(); return true; }
    if (st.m_uncomp_size > (mz_uint64)512 * 1024 * 1024) return false;

    out->resize((size_t)st.m_uncomp_size);
    if (!mz_zip_reader_extract_to_mem(&d_->zip, (mz_uint)index, out->data(), out->size(), 0)) {
        out->clear();
        return false;
    }
    return true;
}

bool ZipReader::readEntry(const std::string &name, std::vector<uint8_t> *out) const
{
    if (!open_ || !d_->zipInited) return false;
    int idx = mz_zip_reader_locate_file(&d_->zip, name.c_str(), nullptr, 0);
    if (idx < 0) idx = mz_zip_reader_locate_file(&d_->zip, name.c_str(), nullptr, MZ_ZIP_FLAG_IGNORE_PATH);
    if (idx < 0) return false;
    return readEntryAt((uint32_t)idx, out);
}

} // namespace apk
