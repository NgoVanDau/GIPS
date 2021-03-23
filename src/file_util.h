#pragma once

#include <cstdint>

namespace FileUtil {

///////////////////////////////////////////////////////////////////////////////

//! get the current working directory
//! \returns a newly-malloc'd string containing the working directory name
//!          (must be free()d by the caller)
char* getCurrentDirectory();

///////////////////////////////////////////////////////////////////////////////

class Directory {
    struct DirectoryPrivate *priv = nullptr;
public:
    bool open(const char* dir);
    inline bool good() const { return (priv != nullptr); }
    void close();

    bool next();
    const char* currentItemName() const;
    bool currentItemIsDir();

    inline bool nextNonDot() {
        bool res;
        const char* n;
        do {
            res = next();
            n = currentItemName();
        } while (res && n && (n[0] == '.'));
        return res;
    }

    inline Directory() {}
    inline Directory(const char* dir) { open(dir); }
    inline ~Directory() { close(); }
};

///////////////////////////////////////////////////////////////////////////////

class FileFingerprint {
    uint64_t m_size  = 0;
    uint64_t m_mtime = 0;
public:
    inline FileFingerprint() {}
    inline FileFingerprint(const char* path) { update(path); }
    inline bool good() const { return m_size || m_mtime; }
    inline bool operator== (const FileFingerprint& other) const
        { return m_size && m_mtime && (m_size == other.m_size) && (m_mtime == other.m_mtime); }
    inline FileFingerprint& operator= (const char* path) { update(path); return *this; }

    bool update(const char* path);
};

///////////////////////////////////////////////////////////////////////////////

}  // namespace FileUtil
