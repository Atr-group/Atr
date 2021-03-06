
#include <ripple/basics/Archive.h>
#include <ripple/basics/contract.h>
#include <archive.h>
#include <archive_entry.h>
namespace ripple {
void
extractTarLz4(
    boost::filesystem::path const& src,
    boost::filesystem::path const& dst)
{
    if (!is_regular_file(src))
        Throw<std::runtime_error>("Invalid source file");
    using archive_ptr =
        std::unique_ptr<struct archive, void(*)(struct archive*)>;
    archive_ptr ar {archive_read_new(),
        [](struct archive* ar)
        {
            archive_read_free(ar);
        }};
    if (!ar)
        Throw<std::runtime_error>("Failed to allocate archive");
    if (archive_read_support_format_tar(ar.get()) < ARCHIVE_OK)
        Throw<std::runtime_error>(archive_error_string(ar.get()));
    if (archive_read_support_filter_lz4(ar.get()) < ARCHIVE_OK)
        Throw<std::runtime_error>(archive_error_string(ar.get()));
    if (archive_read_open_filename(
        ar.get(), src.string().c_str(), 10240) < ARCHIVE_OK)
    {
        Throw<std::runtime_error>(archive_error_string(ar.get()));
    }
    archive_ptr aw {archive_write_disk_new(),
        [](struct archive* aw)
        {
            archive_write_free(aw);
        }};
    if (!aw)
        Throw<std::runtime_error>("Failed to allocate archive");
    if (archive_write_disk_set_options(
        aw.get(),
        ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM |
        ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS) < ARCHIVE_OK)
    {
        Throw<std::runtime_error>(archive_error_string(aw.get()));
    }
    if(archive_write_disk_set_standard_lookup(aw.get()) < ARCHIVE_OK)
        Throw<std::runtime_error>(archive_error_string(aw.get()));
    int result;
    struct archive_entry* entry;
    while(true)
    {
        result = archive_read_next_header(ar.get(), &entry);
        if (result == ARCHIVE_EOF)
            break;
        if (result < ARCHIVE_OK)
            Throw<std::runtime_error>(archive_error_string(ar.get()));
        archive_entry_set_pathname(
            entry, (dst / archive_entry_pathname(entry)).string().c_str());
        if (archive_write_header(aw.get(), entry) < ARCHIVE_OK)
            Throw<std::runtime_error>(archive_error_string(aw.get()));
        if (archive_entry_size(entry) > 0)
        {
            const void *buf;
            size_t sz;
            la_int64_t offset;
            while (true)
            {
                result = archive_read_data_block(ar.get(), &buf, &sz, &offset);
                if (result == ARCHIVE_EOF)
                    break;
                if (result < ARCHIVE_OK)
                    Throw<std::runtime_error>(archive_error_string(ar.get()));
                if (archive_write_data_block(
                    aw.get(), buf, sz, offset) < ARCHIVE_OK)
                {
                    Throw<std::runtime_error>(archive_error_string(aw.get()));
                }
            }
        }
        if (archive_write_finish_entry(aw.get()) < ARCHIVE_OK)
            Throw<std::runtime_error>(archive_error_string(aw.get()));
    }
}
} 
