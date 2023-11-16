#include "pch.h"
#include "HttpCache.h"

#include <winsqlite/winsqlite3.h>

[[noreturn]] inline void throw_sqlite3_error(sqlite3* db) {
    throw winrt::hresult_error(E_FAIL, winrt::hstring(
        std::format(L"sqlite3 error: [{}] {}", sqlite3_errcode(db), (const wchar_t*)sqlite3_errmsg16(db))
    ));
}
[[noreturn]] inline void throw_sqlite3_error_code(int code) {
    throw winrt::hresult_error(E_FAIL, winrt::hstring(
        std::format(L"sqlite3 error: [{}] {}", code, winrt::to_hstring(sqlite3_errstr(code)))
    ));
}
inline void check_sqlite3(sqlite3* db, int result) {
    if (result != SQLITE_OK) { throw_sqlite3_error(db); }
}
inline void check_sqlite3(int result) {
    if (result != SQLITE_OK) { throw_sqlite3_error_code(result); }
}
template<typename U, typename... Args>
inline void check_sqlite3_call(U&& func, sqlite3* db, Args&&... args) {
    if (std::invoke(func, db, std::forward<Args>(args)...) != SQLITE_OK) {
        throw_sqlite3_error(db);
    }
}

struct Sqlite3MutexGuard {
    Sqlite3MutexGuard(sqlite3* db) : Sqlite3MutexGuard(sqlite3_db_mutex(db)) {}
    Sqlite3MutexGuard(sqlite3_mutex* sqlite_mutex) : m_sqlite_mutex(sqlite_mutex) {
        sqlite3_mutex_enter(m_sqlite_mutex);
    }
    ~Sqlite3MutexGuard() {
        sqlite3_mutex_leave(m_sqlite_mutex);
    }
private:
    sqlite3_mutex* m_sqlite_mutex;
};
struct Sqlite3Statement {
    Sqlite3Statement(sqlite3* db, std::wstring_view stmt) : m_db(db) {
        check_sqlite3_call(sqlite3_prepare16_v2,
            m_db, stmt.data(), static_cast<int>(stmt.size() * 2), &m_stmt, nullptr);
    }
    void reset(void) {
        check_sqlite3(sqlite3_reset(m_stmt));
    }
    void bind(int idx, std::wstring_view value, bool clone_ownership) {
        check_sqlite3(sqlite3_bind_text16(
            m_stmt,
            idx,
            value.data(), static_cast<int>(value.size() * 2),
            clone_ownership ? SQLITE_TRANSIENT : SQLITE_STATIC
        ));
    }
    void bind(int idx, int64_t value) {
        check_sqlite3(sqlite3_bind_int64(m_stmt, idx, value));
    }
    bool step(void) {
        {
            // Used for protecting error messages
            Sqlite3MutexGuard guard(m_db);
            switch (sqlite3_step(m_stmt)) {
            case SQLITE_ROW:
                return true;
            case SQLITE_DONE:
                return false;
            case SQLITE_BUSY:
                break;
            default:
                throw_sqlite3_error(m_db);
            }
        }
        // SQLITE_BUSY
        Sleep(0);
        return step();
    }
    int64_t col_i64(int idx) { return sqlite3_column_int64(m_stmt, idx); }
    const wchar_t* col_str16(int idx) {
        return reinterpret_cast<const wchar_t*>(sqlite3_column_text16(m_stmt, idx));
    }
    ~Sqlite3Statement() {
        auto code = sqlite3_finalize(m_stmt);
        if (code != SQLITE_OK) {
            util::debug::log_warn(std::format(L"sqlite3 error: [{}] {}",
                code, winrt::to_hstring(sqlite3_errstr(code))));
        }
    }
private:
    sqlite3* m_db;
    sqlite3_stmt* m_stmt;
};

namespace BiliUWP {
    using namespace winrt::Windows::Foundation;
    using namespace winrt::Windows::Web::Http;
    using namespace winrt::Windows::Storage::Streams;

    // NOTE: Cancellation not supported
    struct details::HttpCacheImpl : std::enable_shared_from_this<HttpCacheImpl> {
        HttpCacheImpl(
            winrt::Windows::Storage::StorageFolder const& root,
            winrt::hstring const& name,
            winrt::Windows::Web::Http::Filters::IHttpFilter const& http_filter
        ) : m_root(root), m_cache_dir(nullptr), m_name(name), m_http_client(nullptr), m_db(nullptr) {
            if (http_filter) {
                m_http_client = winrt::Windows::Web::Http::HttpClient(http_filter);
            }
            else {
                using namespace winrt::Windows::Web::Http::Filters;
                auto filter = HttpBaseProtocolFilter();
                auto cache_control = filter.CacheControl();
                cache_control.ReadBehavior(HttpCacheReadBehavior::NoCache);
                cache_control.WriteBehavior(HttpCacheWriteBehavior::NoCache);
                m_http_client = winrt::Windows::Web::Http::HttpClient(filter);
            }
        }
        ~HttpCacheImpl() {
            if (m_db) {
                // TODO: Wait for all db operations to finish?
                check_sqlite3_call(sqlite3_close, m_db);
            }
        }
        IAsyncOperationWithProgress<IRandomAccessStream, HttpProgress> fetch_async(
            winrt::Windows::Foundation::Uri const& uri
        ) {
            auto progress_token = co_await winrt::get_progress_token();
            auto op = fetch_async_inner(uri, std::nullopt, false, false);
            op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
            co_return (co_await std::move(op)).as<IRandomAccessStream>();
        }
        IAsyncOperationWithProgress<IRandomAccessStream, HttpProgress> fetch_async(
            winrt::Windows::Foundation::Uri const& uri,
            uint64_t override_age
        ) {
            auto progress_token = co_await winrt::get_progress_token();
            auto op = fetch_async_inner(uri, override_age, false, false);
            op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
            co_return (co_await std::move(op)).as<IRandomAccessStream>();
        }
        IAsyncOperationWithProgress<winrt::Windows::Foundation::Uri, HttpProgress> fetch_as_local_uri_async(
            winrt::Windows::Foundation::Uri const& uri
        ) {
            auto progress_token = co_await winrt::get_progress_token();
            auto op = fetch_async_inner(uri, std::nullopt, true, true);
            op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
            co_return (co_await std::move(op)).as<winrt::Windows::Foundation::Uri>();
        }
        IAsyncOperationWithProgress<winrt::Windows::Foundation::Uri, HttpProgress> fetch_as_local_uri_async(
            winrt::Windows::Foundation::Uri const& uri,
            uint64_t override_age
        ) {
            auto progress_token = co_await winrt::get_progress_token();
            auto op = fetch_async_inner(uri, override_age, true, true);
            op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
            co_return (co_await std::move(op)).as<winrt::Windows::Foundation::Uri>();
        }
        IAsyncOperationWithProgress<winrt::Windows::Foundation::Uri, HttpProgress> fetch_as_app_uri_async(
            winrt::Windows::Foundation::Uri const& uri
        ) {
            auto progress_token = co_await winrt::get_progress_token();
            auto op = fetch_async_inner(uri, std::nullopt, true, false);
            op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
            co_return (co_await std::move(op)).as<winrt::Windows::Foundation::Uri>();
        }
        IAsyncOperationWithProgress<winrt::Windows::Foundation::Uri, HttpProgress> fetch_as_app_uri_async(
            winrt::Windows::Foundation::Uri const& uri,
            uint64_t override_age
        ) {
            auto progress_token = co_await winrt::get_progress_token();
            auto op = fetch_async_inner(uri, override_age, true, false);
            op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
            co_return (co_await std::move(op)).as<winrt::Windows::Foundation::Uri>();
        }
        util::winrt::task<> remove_expired_async(void) {
            co_await winrt::resume_background();
            auto cur_ts = get_cur_ts();
            Sqlite3Statement db_stmt(m_db, L"DELETE FROM entries WHERE "
                "? > age + life_start_ts AND delete_entry(path) != 0;");
            db_stmt.bind(1, static_cast<int64_t>(cur_ts));
            db_stmt.step();
        }
        util::winrt::task<> clear_async(void) {
            co_await winrt::resume_background();
            // NOTE: The deletion is likely to fail, as the database connection remains open
            util::fs::delete_all_inside_folder(m_cache_dir_path.c_str());
            //remove_record_all();
            // NOTE: Reduce orphan files at the expense of performance
            Sqlite3Statement(m_db, L"DELETE FROM entries WHERE delete_entry(path) != 0;").step();
        }

    private:
        friend struct HttpCache;

        struct TableRecord {
            uint64_t default_age;
            uint64_t age;
            uint64_t life_start_ts;
        };

        // WARN: Not protected by mutex; caller must guarantee thread safety
        util::winrt::task<> init_async(void) {
            if (m_db) { co_return; }
            co_await winrt::resume_background();
            m_cache_dir = co_await m_root.CreateFolderAsync(
                m_name, winrt::Windows::Storage::CreationCollisionOption::OpenIfExists);
            m_cache_dir_path = m_cache_dir.Path();
            if (m_cache_dir_path.back() != L'\\') {
                m_cache_dir_path = m_cache_dir_path + L'\\';
            }
            {   // Make app uri
                auto app_data = winrt::Windows::Storage::ApplicationData::Current();
                auto app_local_folder = app_data.LocalFolder();
                auto app_temp_folder = app_data.TemporaryFolder();
                auto app_roaming_folder = app_data.RoamingFolder();
                std::wstring dir_buf{ m_name + L'/' };
                auto cur_folder = m_root;
                while (cur_folder) {
                    if (cur_folder.IsEqual(app_local_folder)) {
                        dir_buf = L"ms-appdata:///local/" + std::move(dir_buf);
                        break;
                    }
                    else if (cur_folder.IsEqual(app_temp_folder)) {
                        dir_buf = L"ms-appdata:///temp/" + std::move(dir_buf);
                        break;
                    }
                    else if (cur_folder.IsEqual(app_roaming_folder)) {
                        dir_buf = L"ms-appdata:///roaming/" + std::move(dir_buf);
                        break;
                    }
                    else {
                        dir_buf = cur_folder.Name() + L'/' + std::move(dir_buf);
                    }
                    cur_folder = co_await cur_folder.GetParentAsync();
                }
                if (dir_buf != L"") {
                    if (dir_buf.back() != L'/') {
                        dir_buf += L'/';
                    }
                }
                m_cache_dir_uri_str = dir_buf;
            }
            auto db_path = m_cache_dir_path + L"data.db";
            if (sqlite3_open16(db_path.data(), &m_db) != SQLITE_OK) {
                if (!m_db) {
                    throw winrt::hresult_error(E_OUTOFMEMORY, L"sqlite3 init database out of memory");
                }
                deferred([&] { sqlite3_close(m_db); m_db = nullptr; });
                throw_sqlite3_error(m_db);
            }
            // Create & update the database
            update_database();
            // Install functions
            check_sqlite3_call(sqlite3_create_function, m_db, "delete_entry", 1, SQLITE_UTF16, this,
                [](sqlite3_context* context, int argc, sqlite3_value** argv) noexcept {
                    // NOTE: Non-zero indicates success
                    int result = 1;
                    try {
                        if (argc != 1) {
                            throw winrt::hresult_invalid_argument(
                                L"sql function delete_entry: Expected exactly 1 argument");
                        }
                        auto that = reinterpret_cast<HttpCacheImpl*>(sqlite3_user_data(context));
                        auto path = reinterpret_cast<const wchar_t*>(sqlite3_value_text16(argv[0]));
                        if (!util::fs::delete_file_if_exists((that->m_cache_dir_path + path).c_str())) {
                            throw winrt::hresult_error(E_FAIL,
                                L"sql function delete_entry: Cannot remove file");
                        }
                    }
                    catch (...) {
                        util::winrt::log_current_exception();
                        result = 0;
                    }
                    sqlite3_result_int(context, result);
                }, nullptr, nullptr
            );
        }
        void update_database() {
            Sqlite3Statement stmt_user_version(m_db, L"PRAGMA user_version;");
            if (!stmt_user_version.step()) {
                throw winrt::hresult_error(E_FAIL, L"HttpCache: Cannot retrieve database version");
            }
            auto current_version = stmt_user_version.col_i64(0);
            if (current_version == 0) {
                // Initialize database
                Sqlite3Statement(m_db, L""
                    "CREATE TABLE IF NOT EXISTS entries("
                    "path TEXT UNIQUE PRIMARY KEY NOT NULL,"
                    "default_age INTEGER NOT NULL,"
                    "age INTEGER NOT NULL,"
                    "life_start_ts INTEGER NOT NULL);"
                ).step();
                Sqlite3Statement(m_db, L"PRAGMA user_version = 1;").step();
            }
            else if (current_version == 1) {
                // Database is up-to-date
            }
            else {
                throw winrt::hresult_error(E_FAIL, L"HttpCache: Unrecognized database version");
            }
        }

        void check_uri_scheme(winrt::Windows::Foundation::Uri const& uri) {
            auto uri_scheme_name = uri.SchemeName();
            if (uri_scheme_name != L"http" && uri_scheme_name != L"https") {
                throw winrt::hresult_invalid_argument(L"HttpCache: Only http(s) uri can be fetched");
            }
        }
        winrt::hstring preprocess_uri(winrt::Windows::Foundation::Uri const& uri) {
            check_uri_scheme(uri);
            std::wstring uri_path{ uri.Path() };
            if (uri_path.ends_with(L'/')) {
                throw winrt::hresult_invalid_argument(L"HttpCache: Requested resource is not a file");
            }
            /*for (auto& ch : uri_path) {
                if (ch != L'/') { continue; }
                ch = L'\\';
            }*/
            auto uri_host = uri.Host();
            auto uri_query = uri.Query();
            if (uri_query == L"") {
                return winrt::hstring(std::format(L"{}{}", uri_host, uri_path));
            }
            return winrt::hstring(std::format(L"{}{}@{}", uri_host, uri_path, uri_query));
        }

        static uint64_t get_cur_ts(void) {
            return static_cast<uint64_t>(std::chrono::floor<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }

        std::optional<TableRecord> lookup_record(winrt::hstring const& key) {
            Sqlite3Statement db_stmt(m_db,
                L"SELECT default_age, age, life_start_ts FROM entries WHERE path = ?;");
            db_stmt.bind(1, key, false);
            if (!db_stmt.step()) { return std::nullopt; }
            return TableRecord{
                .default_age = static_cast<uint64_t>(db_stmt.col_i64(0)),
                .age = static_cast<uint64_t>(db_stmt.col_i64(1)),
                .life_start_ts = static_cast<uint64_t>(db_stmt.col_i64(2)),
            };
        }
        void remove_record(winrt::hstring const& key) {
            Sqlite3Statement db_stmt(m_db, L"DELETE FROM entries WHERE path = ?;");
            db_stmt.bind(1, key, false);
            db_stmt.step();
        }
        void insert_record(winrt::hstring const& key, TableRecord const& new_record) {
            Sqlite3Statement db_stmt(m_db, L"INSERT INTO entries VALUES(?, ?, ?, ?);");
            db_stmt.bind(1, key, false);
            db_stmt.bind(2, static_cast<int64_t>(new_record.default_age));
            db_stmt.bind(3, static_cast<int64_t>(new_record.age));
            db_stmt.bind(4, static_cast<int64_t>(new_record.life_start_ts));
            db_stmt.step();
        }
        void update_record(winrt::hstring const& key, TableRecord const& new_record) {
            Sqlite3Statement db_stmt(m_db,
                L"UPDATE entries SET default_age = ?, age = ?, life_start_ts = ? WHERE path = ?;");
            db_stmt.bind(1, static_cast<int64_t>(new_record.default_age));
            db_stmt.bind(2, static_cast<int64_t>(new_record.age));
            db_stmt.bind(3, static_cast<int64_t>(new_record.life_start_ts));
            db_stmt.bind(4, key, false);
            db_stmt.step();
        }
        void remove_record_all(void) {
            Sqlite3Statement(m_db, L"DELETE FROM entries;").step();
        }
        /*
        template<typename Functor>
        void iterate_records(Functor&& functor) {
            Sqlite3Statement db_stmt(m_db, L"SELECT * FROM entries;");
            while (db_stmt.step()) {
                auto record = TableRecord{
                    .default_age = static_cast<uint64_t>(db_stmt.col_i64(1)),
                    .age = static_cast<uint64_t>(db_stmt.col_i64(2)),
                    .life_start_ts = static_cast<uint64_t>(db_stmt.col_i64(3)),
                };
                functor(db_stmt.col_str16(0), std::move(record));
            }
        }
        */

        IAsyncOperationWithProgress<IInspectable, HttpProgress> fetch_async_inner(
            winrt::Windows::Foundation::Uri uri,
            std::optional<uint64_t> override_age,
            bool uri_as_result,
            bool uri_return_abs
        ) {
            // TODO: Issue: https://github.com/microsoft/microsoft-ui-xaml/issues/633
            auto progress_token = co_await winrt::get_progress_token();

            auto local_path = preprocess_uri(uri);
            co_await winrt::resume_background();
            auto cur_ts = get_cur_ts();
            /*  Pseudocode:
            *   open;
            *   if (open failed) {
            *       check(create dirs);
            *       open;
            *   }
            *   if (open failed) { throw; }
            *   while (true) {
            *       lock file shared;
            *       if (lookup db && not expired) { return (file as stream); }
            *       unlock file;
            *       lock file unique;
            *       if (lookup db && not expired) {
            *           unlock file;
            *           continue;
            *       }
            *       fetch remote resource;
            *       write file;
            *       update db;
            *       unlock file;
            *   }
            */
            //auto full_path = m_cache_dir_path + local_path;
            std::wstring full_path{ local_path };
            for (auto& ch : full_path) {
                if (ch != L'/') { continue; }
                ch = L'\\';
            }
            full_path = m_cache_dir_path + std::move(full_path);
            auto create_dirs_fn = [&] {
                std::wstring_view full_path_view = full_path;
                winrt::hstring path_base_dir{ full_path_view.substr(0, full_path_view.rfind(L'\\')) };
                if (!util::fs::create_dir_all(path_base_dir.c_str())) {
                    throw winrt::hresult_error(E_FAIL, L"HttpCache: Failed to create directories");
                }
            };
            auto open_file_fn = [=] {
                // NOTE: Don't open in asynchronous mode
                return CreateFile2FromAppW(
                    full_path.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    OPEN_ALWAYS,
                    nullptr
                );
            };
            auto lock_file_fn = [](winrt::file_handle const& hfile, bool exclusive) {
                OVERLAPPED ol;
                ol.hEvent = nullptr;
                ol.Offset = 0;
                ol.OffsetHigh = 0;
                winrt::check_bool(LockFileEx(
                    hfile.get(),
                    exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0,
                    0,
                    16, 0,
                    &ol
                ));
            };
            auto unlock_file_fn = [](winrt::file_handle const& hfile) {
                OVERLAPPED ol;
                ol.hEvent = nullptr;
                ol.Offset = 0;
                ol.OffsetHigh = 0;
                winrt::check_bool(UnlockFileEx(hfile.get(), 0, 16, 0, &ol));
            };
            auto is_file_empty_fn = [](winrt::file_handle const& hfile) {
                LARGE_INTEGER li;
                winrt::check_bool(GetFileSizeEx(hfile.get(), &li));
                return li.QuadPart == 0;
            };
            auto is_entry_record_fresh = [&] {
                auto ov = lookup_record(local_path);
                if (!ov) { return false; }
                if (cur_ts > ov->age + ov->life_start_ts) { return false; }
                return true;
            };
            winrt::file_handle hfile{ open_file_fn() };
            if (!hfile) {
                create_dirs_fn();
                *hfile.put() = open_file_fn();
            }
            if (!hfile) { winrt::throw_last_error(); }
            auto require_fetch_fn = [&] {
                return is_file_empty_fn(hfile) || !is_entry_record_fresh();
            };
            const uint32_t MAX_LOOP_CNT = 10;
            uint32_t loop_cnt = 0;
            while (loop_cnt++ < MAX_LOOP_CNT) {
                lock_file_fn(hfile, false);
                if (!require_fetch_fn()) {
                    if (uri_as_result) {
                        if (uri_return_abs) {
                            co_return Uri(full_path);
                        }
                        std::wstring local_path_buf{ local_path };
                        for (auto& i : local_path_buf) {
                            if (i != L'\\') { continue; }
                            i = L'/';
                        }
                        co_return Uri(m_cache_dir_uri_str, local_path_buf);
                    }
                    struct Win32FileReadOnlyRandomAccessStream :
                        winrt::implements<Win32FileReadOnlyRandomAccessStream,
                        IRandomAccessStream, IClosable, IInputStream, IOutputStream>
                    {
                        Win32FileReadOnlyRandomAccessStream(
                            winrt::file_handle hfile,
                            std::function<winrt::file_handle(winrt::file_handle const&)> file_cloner) :
                            m_hfile(std::move(hfile)), m_file_cloner(std::move(file_cloner)) {}
                        Win32FileReadOnlyRandomAccessStream(winrt::file_handle hfile,
                            std::function<winrt::file_handle(winrt::file_handle const&)> file_cloner,
                            uint64_t position) :
                            Win32FileReadOnlyRandomAccessStream(std::move(hfile), std::move(file_cloner))
                        {
                            this->Seek(position);
                        }
                        ~Win32FileReadOnlyRandomAccessStream() { Close(); }
                        void Close() { m_hfile.close(); }
                        IAsyncOperationWithProgress<IBuffer, uint32_t> ReadAsync(
                            IBuffer buffer, uint32_t count, InputStreamOptions options
                        ) {
                            // TODO: Maybe optimize thread scheduling
                            //co_await winrt::resume_background();
                            buffer.Length(count);
                            auto actual_size = buffer.Length();
                            DWORD dwRead;
                            winrt::check_bool(ReadFile(
                                m_hfile.get(), buffer.data(), actual_size, &dwRead, nullptr));
                            buffer.Length(dwRead);
                            co_return buffer;
                        }
                        IAsyncOperationWithProgress<uint32_t, uint32_t> WriteAsync(IBuffer const& buffer) {
                            throw winrt::hresult_illegal_method_call();
                        }
                        IAsyncOperation<bool> FlushAsync() {
                            co_return true;
                        }
                        uint64_t Size() {
                            LARGE_INTEGER li;
                            winrt::check_bool(GetFileSizeEx(m_hfile.get(), &li));
                            return static_cast<uint64_t>(li.QuadPart);
                        }
                        void Size(uint64_t value) {
                            throw winrt::hresult_illegal_method_call();
                        }
                        ::winrt::Windows::Storage::Streams::IInputStream GetInputStreamAt(uint64_t position) {
                            return winrt::make<Win32FileReadOnlyRandomAccessStream>(
                                m_file_cloner(m_hfile), m_file_cloner, position);
                        }
                        ::winrt::Windows::Storage::Streams::IOutputStream GetOutputStreamAt(uint64_t position) {
                            throw winrt::hresult_illegal_method_call();
                        }
                        uint64_t Position() {
                            LARGE_INTEGER li;
                            winrt::check_bool(SetFilePointerEx(m_hfile.get(), {}, &li, FILE_CURRENT));
                            return static_cast<uint64_t>(li.QuadPart);
                        }
                        void Seek(uint64_t position) {
                            winrt::check_bool(SetFilePointerEx(
                                m_hfile.get(),
                                { .QuadPart = static_cast<LONGLONG>(position) },
                                nullptr,
                                FILE_BEGIN)
                            );
                        }
                        IRandomAccessStream CloneStream() {
                            return winrt::make<Win32FileReadOnlyRandomAccessStream>(
                                m_file_cloner(m_hfile), m_file_cloner);
                        }
                        bool CanRead() { return true; }
                        bool CanWrite() { return false; }
                    private:
                        winrt::file_handle m_hfile;
                        std::function<winrt::file_handle(winrt::file_handle const&)> m_file_cloner;
                    };
                    co_return winrt::make<Win32FileReadOnlyRandomAccessStream>(std::move(hfile),
                        [=](winrt::file_handle const&) {
                            winrt::file_handle hfile{ open_file_fn() };
                            if (!hfile) { winrt::throw_last_error(); }
                            lock_file_fn(hfile, false);
                            return hfile;
                        }
                    );
                    /*auto file_stream = winrt::make<Win32FileReadOnlyRandomAccessStream>(std::move(hfile),
                        [=](winrt::file_handle const&) {
                            winrt::file_handle hfile{ open_file_fn() };
                            if (!hfile) { winrt::throw_last_error(); }
                            lock_file_fn(hfile, false);
                            return hfile;
                        }
                    );
                    InMemoryRandomAccessStream mem_stream;
                    mem_stream.Size(file_stream.Size());
                    RandomAccessStream::CopyAsync(file_stream, mem_stream);
                    co_return mem_stream;*/
                }
                unlock_file_fn(hfile);
                lock_file_fn(hfile, true);
                if (!require_fetch_fn()) {
                    unlock_file_fn(hfile);
                    continue;
                }
                deferred([&] { unlock_file_fn(hfile); });
                // Fetch & store resource
                uint64_t res_max_age;
                {
                    util::debug::log_trace(std::format(L"HttpCache: Fetching resource `{}`...", uri.ToString()));
                    auto http_req = HttpRequestMessage();
                    http_req.Method(HttpMethod::Get());
                    http_req.RequestUri(uri);
                    auto http_resp = co_await m_http_client.SendRequestAsync(
                        http_req, HttpCompletionOption::ResponseHeadersRead
                    );
                    http_resp.EnsureSuccessStatusCode();
                    auto http_resp_hdr = http_resp.Headers();
                    auto http_resp_cache_control_hdr = http_resp_hdr.CacheControl();
                    auto nullable_max_age = http_resp_cache_control_hdr.MaxAge();
                    if (nullable_max_age) {
                        res_max_age = static_cast<uint64_t>(std::chrono::floor<std::chrono::seconds>(
                            nullable_max_age.Value()).count());
                    }
                    else {
                        res_max_age = 0;
                    }
                    struct Win32FileOutputStream :
                        winrt::implements<Win32FileOutputStream, IOutputStream, IClosable>
                    {
                        Win32FileOutputStream(winrt::file_handle const& hfile) : m_hfile(hfile) {}
                        void Close() {}
                        IAsyncOperationWithProgress<uint32_t, uint32_t> WriteAsync(IBuffer const& buffer) {
                            auto progress_token = co_await winrt::get_progress_token();
                            // TODO: Maybe avoid blocked reading?
                            auto data_len = buffer.Length();
                            DWORD dwWritten;
                            progress_token(0);
                            WriteFile(m_hfile.get(), buffer.data(), data_len, &dwWritten, nullptr);
                            progress_token(dwWritten);
                            co_return dwWritten;
                        }
                        IAsyncOperation<bool> FlushAsync() {
                            co_return FlushFileBuffers(m_hfile.get());
                        }
                    private:
                        winrt::file_handle const& m_hfile;
                    };
                    auto http_cont = http_resp.Content();
                    auto op = http_cont.WriteToStreamAsync(winrt::make<Win32FileOutputStream>(hfile));
                    HttpProgress http_progress{
                        .Stage = HttpProgressStage::ReceivingContent,
                        .BytesSent = 0,
                        .TotalBytesToSend = nullptr,
                        .BytesReceived = 0,
                        .TotalBytesToReceive = http_cont.Headers().ContentLength(),
                        .Retries = loop_cnt - 1,
                    };
                    progress_token(http_progress);
                    op.Progress([&](auto const&, uint64_t progress) {
                        http_progress.BytesReceived = progress;
                        progress_token(http_progress);
                    });
                    co_await std::move(op);
                    SetFilePointer(hfile.get(), 0, nullptr, FILE_BEGIN);
                }
                if (is_file_empty_fn(hfile)) {
                    throw winrt::hresult_not_implemented(L"HttpCache does not support caching empty files");
                }
                // Update database
                if (auto ov = lookup_record(local_path)) {
                    ov->default_age = res_max_age;
                    ov->age = override_age.value_or(res_max_age);
                    ov->life_start_ts = cur_ts;
                    update_record(local_path, *ov);
                }
                else {
                    TableRecord rec{
                        .default_age = res_max_age,
                        .age = override_age.value_or(res_max_age),
                        .life_start_ts = cur_ts,
                    };
                    insert_record(local_path, rec);
                }
            }
            throw winrt::hresult_error(E_FAIL, L"Detected potential loop bug in HttpCache::fetch_async");
        }

        winrt::Windows::Storage::StorageFolder m_root;
        winrt::Windows::Storage::StorageFolder m_cache_dir;
        winrt::hstring m_cache_dir_uri_str;
        winrt::hstring m_name;
        winrt::hstring m_cache_dir_path;
        winrt::Windows::Web::Http::HttpClient m_http_client;
        sqlite3* m_db;
    };

    util::winrt::task<HttpCache> HttpCache::create_async(
        winrt::Windows::Storage::StorageFolder const& root,
        winrt::hstring const& name,
        winrt::Windows::Web::Http::Filters::IHttpFilter const& http_filter
    ) {
        HttpCache result(nullptr);
        result.m_impl = std::make_shared<details::HttpCacheImpl>(root, name, http_filter);
        co_await result.m_impl->init_async();
        co_return result;
    }
    IAsyncOperationWithProgress<IRandomAccessStream, HttpProgress> HttpCache::fetch_async(
        winrt::Windows::Foundation::Uri const& uri
    ) const {
        auto strong_this = m_impl;
        auto progress_token = co_await winrt::get_progress_token();
        auto op = strong_this->fetch_async(uri);
        op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
        co_return co_await std::move(op);
    }
    IAsyncOperationWithProgress<IRandomAccessStream, HttpProgress> HttpCache::fetch_async(
        winrt::Windows::Foundation::Uri const& uri,
        uint64_t override_age   // In seconds
    ) const {
        auto strong_this = m_impl;
        auto progress_token = co_await winrt::get_progress_token();
        auto op = strong_this->fetch_async(uri, override_age);
        op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
        co_return co_await std::move(op);
    }
    IAsyncOperationWithProgress<winrt::Windows::Foundation::Uri, HttpProgress> HttpCache::fetch_as_local_uri_async(
        winrt::Windows::Foundation::Uri const& uri
    ) const {
        auto strong_this = m_impl;
        auto progress_token = co_await winrt::get_progress_token();
        auto op = strong_this->fetch_as_local_uri_async(uri);
        op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
        co_return co_await std::move(op);
    }
    IAsyncOperationWithProgress<winrt::Windows::Foundation::Uri, HttpProgress> HttpCache::fetch_as_local_uri_async(
        winrt::Windows::Foundation::Uri const& uri,
        uint64_t override_age   // In seconds
    ) const {
        auto strong_this = m_impl;
        auto progress_token = co_await winrt::get_progress_token();
        auto op = strong_this->fetch_as_local_uri_async(uri, override_age);
        op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
        co_return co_await std::move(op);
    }
    IAsyncOperationWithProgress<winrt::Windows::Foundation::Uri, HttpProgress> HttpCache::fetch_as_app_uri_async(
        winrt::Windows::Foundation::Uri const& uri
    ) const {
        auto strong_this = m_impl;
        auto progress_token = co_await winrt::get_progress_token();
        auto op = strong_this->fetch_as_app_uri_async(uri);
        op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
        co_return co_await std::move(op);
    }
    IAsyncOperationWithProgress<winrt::Windows::Foundation::Uri, HttpProgress> HttpCache::fetch_as_app_uri_async(
        winrt::Windows::Foundation::Uri const& uri,
        uint64_t override_age   // In seconds
    ) const {
        auto strong_this = m_impl;
        auto progress_token = co_await winrt::get_progress_token();
        auto op = strong_this->fetch_as_app_uri_async(uri, override_age);
        op.Progress([&](auto&&, auto&& progress) { progress_token(progress); });
        co_return co_await std::move(op);
    }
    util::winrt::task<> HttpCache::remove_expired_async(void) const {
        auto strong_this = m_impl;
        co_return co_await strong_this->remove_expired_async();
    }
    util::winrt::task<> HttpCache::clear_async(void) const {
        auto strong_this = m_impl;
        co_return co_await strong_this->clear_async();
    }
}
