#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "types.h"
#include "blood.h"
#include "donors.h"
#include "storage.h"
#include "users.h"
}

extern "C" void *__real_malloc(size_t size);

extern "C" {
bool g_fail_next_malloc = false;
size_t g_fail_malloc_size = 0U;

void *__wrap_malloc(size_t size)
{
    if (g_fail_next_malloc && ((g_fail_malloc_size == 0U) || (size == g_fail_malloc_size))) {
        g_fail_next_malloc = false;
        return nullptr;
    }
    return __real_malloc(size);
}
}

/* ------------------------------------------------------------------- */
/* Helpers                                                               */
/* ------------------------------------------------------------------- */
namespace fs = std::filesystem;

static const char *const ALL_NAMES[] = {
    "donors.txt", "batches.txt", "requests.txt", "users.txt",
    "donors.tmp", "batches.tmp", "requests.tmp", "users.tmp"
};

/* Every test runs inside its own empty temporary folder. */
class StorageTests : public ::testing::Test {
protected:
    void SetUp() override
    {
        static unsigned int counter = 0U;
        original_ = fs::current_path();
        temporary_ = fs::temp_directory_path() /
                     ("bloodbank-storage-" + std::to_string(static_cast<long long>(getpid())) +
                      "-" + std::to_string(counter++));
        ASSERT_TRUE(fs::create_directory(temporary_));
        fs::current_path(temporary_);
    }

    void TearDown() override
    {
        fs::current_path(original_);
        std::error_code ignored;
        fs::remove_all(temporary_, ignored);
    }

    /* Removes every data/temp file (files, folders and links) so a new case starts clean. */
    static void clean()
    {
        for (const char *name : ALL_NAMES) {
            std::error_code ignored;
            fs::remove_all(name, ignored);
        }
    }

    static void write_file(const char *name, const std::string &content)
    {
        std::ofstream out(name, std::ios::trunc);
        out << content;
    }

    static std::string read_file(const char *name)
    {
        std::ifstream in(name);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    /* A file name that points to itself: fopen fails with ELOOP (not ENOENT). */
    static void make_unreadable(const char *name)
    {
        std::error_code ignored;
        fs::remove(name, ignored);
        ASSERT_EQ(symlink(name, name), 0);
    }

    /* load_all() while hiding what it prints on stderr. */
    static int try_load(Database *db, std::string *err = nullptr)
    {
        ::testing::internal::CaptureStderr();
        const int result = load_all(db);
        const std::string captured = ::testing::internal::GetCapturedStderr();
        if (err != nullptr) {
            *err = captured;
        }
        return result;
    }

    /* Same, but the next malloc of this exact size fails. */
    static int try_load_no_memory(Database *db, size_t size)
    {
        ::testing::internal::CaptureStderr();
        g_fail_malloc_size = size;
        g_fail_next_malloc = true;
        const int result = load_all(db);
        g_fail_next_malloc = false;
        g_fail_malloc_size = 0U;
        (void)::testing::internal::GetCapturedStderr();
        return result;
    }

    /* Loads and frees; returns load_all()'s answer. */
    static int load_result(void)
    {
        Database db = {};
        const int result = try_load(&db);
        free_database(&db);
        return result;
    }

    fs::path original_;
    fs::path temporary_;
};

static Donor *add_donor_node(Database *db, int id, const char *name, int active)
{
    Donor *node = static_cast<Donor *>(std::calloc(1, sizeof(Donor)));
    node->id = id;
    (void)std::strcpy(node->name, name);
    (void)std::strcpy(node->phone, "9876543210");
    node->group = 2;
    node->birth_year = 1990;
    node->active = active;
    node->last_donation = 77L;
    node->registered_at = 5L;
    node->deactivated_at = (active != 0) ? 0L : 9L;
    Donor **link = &db->donors;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
    return node;
}

static void add_batch_node(Database *db, int id)
{
    Batch *node = static_cast<Batch *>(std::calloc(1, sizeof(Batch)));
    node->id = id;
    node->donor_id = 1;
    node->group = 2;
    node->ml_collected = 300;
    node->ml_left = 250;
    node->collected = 1000L;
    node->expiry = 2000L;
    node->status = BATCH_AVAILABLE;
    Batch **link = &db->batches;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
}

static void add_request_node(Database *db, int id)
{
    Request *node = static_cast<Request *>(std::calloc(1, sizeof(Request)));
    node->id = id;
    (void)std::strcpy(node->hospital, "City Hospital");
    node->group = 2;
    node->ml = 100;
    node->date = 1500L;
    node->status = REQUEST_PENDING;
    Request **link = &db->requests;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
}

static void add_user_node(Database *db, const char *username, unsigned char salt0, unsigned char hash0)
{
    User *node = static_cast<User *>(std::calloc(1, sizeof(User)));
    (void)std::strcpy(node->username, username);
    node->salt[0] = salt0;
    node->salt[SALT_SIZE - 1] = 0xabU;
    node->hash[0] = hash0;
    node->hash[HASH_SIZE - 1] = 0xcdU;
    User **link = &db->users;
    while (*link != nullptr) {
        link = &(*link)->next;
    }
    *link = node;
}

static std::string hex_zeros(int bytes)
{
    return std::string(static_cast<size_t>(bytes) * 2U, '0');
}

static const std::string GOOD_USER = "staff|" + hex_zeros(SALT_SIZE) + "|" + hex_zeros(HASH_SIZE) + "\n";
static const char *const GOOD_DONOR = "1|Avery Donor|9876543210|2|1990|1|0|1|0\n";
static const char *const GOOD_BATCH = "1|1|2|300|250|1000|2000|1\n";
static const char *const GOOD_REQUEST = "1|City Hospital|2|100|1500|0\n";

/* ------------------------------------------------------------------- */
/* One test per public function                                          */
/* ------------------------------------------------------------------- */

TEST_F(StorageTests, SaveAll)
{
    /* an empty database still writes four (empty) files */
    Database empty = {};
    ASSERT_EQ(save_all(&empty), 1);
    for (const char *name : {"donors.txt", "batches.txt", "requests.txt", "users.txt"}) {
        EXPECT_TRUE(fs::exists(name)) << name;
        EXPECT_EQ(fs::file_size(name), 0U) << name;
    }
    for (const char *name : {"donors.tmp", "batches.tmp", "requests.tmp", "users.tmp"}) {
        EXPECT_FALSE(fs::exists(name)) << name;                   /* temp files were renamed */
    }

    /* a filled database is written one line per record, '|' between fields */
    Database db = {};
    add_donor_node(&db, 1, "Avery Donor", 1);
    add_donor_node(&db, 2, "Second Donor", 0);
    add_batch_node(&db, 1);
    add_batch_node(&db, 2);
    add_request_node(&db, 1);
    add_user_node(&db, "staff", 10U, 20U);
    add_user_node(&db, "admin", 0x0fU, 0xf0U);
    ASSERT_EQ(save_all(&db), 1);

    EXPECT_EQ(read_file("donors.txt"),
              "1|Avery Donor|9876543210|2|1990|1|77|5|0\n"
              "2|Second Donor|9876543210|2|1990|0|77|5|9\n");
    EXPECT_EQ(read_file("batches.txt"),
              "1|1|2|300|250|1000|2000|1\n"
              "2|1|2|300|250|1000|2000|1\n");
    EXPECT_EQ(read_file("requests.txt"), "1|City Hospital|2|100|1500|0\n");
    const std::string users = read_file("users.txt");
    EXPECT_EQ(users.find("staff|0a"), 0U);
    EXPECT_NE(users.find("\nadmin|0f"), std::string::npos);
    EXPECT_EQ(std::count(users.begin(), users.end(), '\n'), 2);

    struct stat info = {};
    ASSERT_EQ(stat("users.txt", &info), 0);
    EXPECT_EQ(info.st_mode & 0777U, 0600U);                       /* only the owner can read it */

    /* what was saved can be loaded again, unchanged */
    Database loaded = {};
    ASSERT_EQ(try_load(&loaded), 1);
    ASSERT_NE(loaded.donors, nullptr);
    EXPECT_STREQ(loaded.donors->name, "Avery Donor");
    ASSERT_NE(loaded.users, nullptr);
    EXPECT_EQ(loaded.users->salt[0], 10U);
    EXPECT_EQ(loaded.users->hash[HASH_SIZE - 1], 0xcdU);
    free_database(&loaded);

    /* the temporary files cannot be created: every file reports its own error */
    clean();
    for (const char *name : {"donors.tmp", "batches.tmp", "requests.tmp", "users.tmp"}) {
        ASSERT_TRUE(fs::create_directory(name));
    }
    ::testing::internal::CaptureStderr();
    EXPECT_EQ(save_all(&db), 0);
    std::string err = ::testing::internal::GetCapturedStderr();
    for (const char *name : {"donors.tmp", "batches.tmp", "requests.tmp", "users.tmp"}) {
        EXPECT_NE(err.find(std::string("could not create ") + name), std::string::npos) << name;
    }

    /* the final files cannot be replaced (a folder is in the way): rename fails */
    clean();
    for (const char *name : {"donors.txt", "batches.txt", "requests.txt", "users.txt"}) {
        ASSERT_TRUE(fs::create_directory(name));
    }
    ::testing::internal::CaptureStderr();
    EXPECT_EQ(save_all(&db), 0);
    err = ::testing::internal::GetCapturedStderr();
    for (const char *name : {"donors.txt", "batches.txt", "requests.txt", "users.txt"}) {
        EXPECT_NE(err.find(std::string("could not write ") + name), std::string::npos) << name;
    }

    /* the disk is full: closing the file fails (small data stays in the buffer until fclose) */
    clean();
    ASSERT_EQ(symlink("/dev/full", "donors.tmp"), 0);
    ::testing::internal::CaptureStderr();
    EXPECT_EQ(save_all(&db), 0);
    err = ::testing::internal::GetCapturedStderr();
    EXPECT_NE(err.find("could not write donors.txt"), std::string::npos);
    EXPECT_TRUE(fs::exists("batches.txt"));                       /* the other files still saved */

    /* the disk is full: writing fails while the file is still open (more than one buffer) */
    clean();
    ASSERT_EQ(symlink("/dev/full", "donors.tmp"), 0);
    for (int id = 3; id < 200; id++) {
        add_donor_node(&db, id, "A donor with a reasonably long name", 1);
    }
    ::testing::internal::CaptureStderr();
    EXPECT_EQ(save_all(&db), 0);
    err = ::testing::internal::GetCapturedStderr();
    EXPECT_NE(err.find("could not write donors.txt"), std::string::npos);

    /* and everything works again once the problem is gone */
    clean();
    EXPECT_EQ(save_all(&db), 1);

    free_database(&db);
}

TEST_F(StorageTests, LoadAll)
{
    std::string err;

    /* ---------- no files at all: a fresh install ---------- */
    EXPECT_EQ(load_result(), 1);

    /* ---------- valid files of every kind ---------- */
    write_file("donors.txt", GOOD_DONOR);
    write_file("batches.txt", GOOD_BATCH);
    write_file("requests.txt", GOOD_REQUEST);
    write_file("users.txt", GOOD_USER);
    {
        Database db = {};
        ASSERT_EQ(try_load(&db), 1);
        ASSERT_NE(db.donors, nullptr);
        EXPECT_STREQ(db.donors->name, "Avery Donor");
        EXPECT_EQ(db.donors->registered_at, 1L);
        ASSERT_NE(db.batches, nullptr);
        EXPECT_EQ(db.batches->ml_left, 250);
        EXPECT_EQ(db.batches->status, BATCH_AVAILABLE);
        ASSERT_NE(db.requests, nullptr);
        EXPECT_STREQ(db.requests->hospital, "City Hospital");
        ASSERT_NE(db.users, nullptr);
        EXPECT_STREQ(db.users->username, "staff");
        EXPECT_EQ(db.users->hash[0], 0U);
        free_database(&db);
    }
    clean();

    /* ---------- donors: blank lines, CRLF, old 7-field rows, several rows ---------- */
    write_file("donors.txt",
               "\n"
               " \t\n"
               "1|Old Active|1234567890|0|1990|1|0\r\n"
               "2|Old Inactive|1234567890|1|1990|0|0\n"
               "3|New Donor|1234567890|7|1985|1|55|66|0\n"
               "4|New Inactive|1234567890|0|1985|0|55|66|99\n");
    {
        Database db = {};
        ASSERT_EQ(try_load(&db), 1);
        const Donor *d = db.donors;
        ASSERT_NE(d, nullptr);
        EXPECT_GT(d->registered_at, 0L);                          /* old rows get today's date  */
        EXPECT_EQ(d->deactivated_at, 0L);                         /* old active row             */
        ASSERT_NE(d->next, nullptr);
        EXPECT_GT(d->next->deactivated_at, 0L);                   /* old inactive row           */
        EXPECT_NE(find_donor(&db, 3), nullptr);
        EXPECT_EQ(find_donor(&db, 3)->registered_at, 66L);
        EXPECT_EQ(find_donor(&db, 4)->deactivated_at, 99L);
        EXPECT_EQ(find_donor(&db, 4)->next, nullptr);
        free_database(&db);
    }
    clean();

    /* ---------- donors: every kind of damage is refused ---------- */
    const char *const bad_donors[] = {
        "0|Bad|1234567890|0|1990|1|0|1|0\n",                      /* id below 1                 */
        "-4|Bad|1234567890|0|1990|1|0|1|0\n",
        "1|One|1234567890|0|1990|1|0|1|0\n"
        "1|Two|1234567890|0|1990|1|0|1|0\n",                      /* duplicate id               */
        "1|Bad|1234567890|8|1990|1|0|1|0\n",                      /* group too high             */
        "1|Bad|1234567890|-1|1990|1|0|1|0\n",                     /* group negative             */
        "1|Bad|1234567890|0|1990|2|0|1|0\n",                      /* active is not 0 or 1       */
        "1|Bad|1234567890|0|1990|1|0|-1|0\n",                     /* negative registered_at     */
        "1|Bad|1234567890|0|1990|0|0|1|-5\n",                     /* negative deactivated_at    */
        "1|Bad|1234567890|0|1990|1|0|1|7\n",                      /* active but has a date      */
        "1|Bad|1234567890|0|1990|0|0|1|0\n",                      /* inactive without a date    */
        "invalid record\n",                                       /* not a record               */
        "1|Bad|1234567890|0|1990|1|0|1|0|extra\n",                /* 9 fields plus junk         */
        "1|Bad|1234567890|0|1990|1|0 junk\n",                     /* 7 fields plus junk         */
        "1|Bad|1234567890|0|1990|1\n",                            /* too few fields             */
        "1|Good|1234567890|0|1990|1|0|1|0\n"
        "this line is damaged\n",                                /* damage after a good record */
    };
    for (const char *content : bad_donors) {
        write_file("donors.txt", content);
        EXPECT_EQ(load_result(), 0) << content;
    }
    clean();

    /* a good file is not loaded any further when an earlier file is damaged */
    write_file("donors.txt", "invalid record\n");
    write_file("batches.txt", GOOD_BATCH);
    {
        Database db = {};
        EXPECT_EQ(try_load(&db), 0);
        EXPECT_EQ(db.batches, nullptr);
        free_database(&db);
    }
    clean();

    /* ---------- batches ---------- */
    write_file("batches.txt",
               "1|1|2|300|250|1000|2000|0\n"
               "2|1|3|300|0|1000|2000|2\n"
               "3|1|0|300|300|1000|2000|1\n");
    {
        Database db = {};
        ASSERT_EQ(try_load(&db), 1);
        ASSERT_NE(db.batches, nullptr);
        ASSERT_NE(db.batches->next, nullptr);
        ASSERT_NE(db.batches->next->next, nullptr);
        EXPECT_EQ(db.batches->status, BATCH_WAITING);
        EXPECT_EQ(db.batches->next->status, BATCH_DISCARDED);
        EXPECT_EQ(db.batches->next->next->ml_left, 300);
        free_database(&db);
    }
    clean();

    const char *const bad_batches[] = {
        "0|1|2|300|250|1000|2000|1\n",                            /* id below 1                 */
        "1|1|2|300|250|1000|2000|1\n"
        "1|1|2|300|250|1000|2000|1\n",                            /* duplicate id               */
        "1|1|8|300|250|1000|2000|1\n",                            /* group too high             */
        "1|1|-1|300|250|1000|2000|1\n",                           /* group negative             */
        "1|1|2|300|-1|1000|2000|1\n",                             /* negative ml_left           */
        "1|1|2|300|301|1000|2000|1\n",                            /* more left than collected   */
        "1|1|2|300|250|1000|2000|-1\n",                           /* status below range         */
        "1|1|2|300|250|1000|2000|3\n",                            /* status above range         */
        "malformed batch\n",                                      /* not a record               */
        "1|1|2|300|250|1000|2000|1\n"
        "malformed batch\n",                                      /* damage after a good record */
    };
    for (const char *content : bad_batches) {
        write_file("batches.txt", content);
        EXPECT_EQ(load_result(), 0) << content;
    }
    clean();

    /* ---------- requests ---------- */
    write_file("requests.txt",
               "1|City Hospital|2|100|1500|0\n"
               "2|Another Hospital|7|200|1600|1\n"
               "3|Third Hospital|0|50|1700|2\n");
    {
        Database db = {};
        ASSERT_EQ(try_load(&db), 1);
        ASSERT_NE(db.requests, nullptr);
        ASSERT_NE(db.requests->next, nullptr);
        ASSERT_NE(db.requests->next->next, nullptr);
        EXPECT_EQ(db.requests->next->status, REQUEST_FULFILLED);
        EXPECT_EQ(db.requests->next->next->status, REQUEST_REJECTED);
        free_database(&db);
    }
    clean();

    const char *const bad_requests[] = {
        "0|City Hospital|2|100|1500|0\n",                         /* id below 1                 */
        "1|City Hospital|2|100|1500|0\n"
        "1|City Hospital|2|100|1500|0\n",                         /* duplicate id               */
        "1|City Hospital|8|100|1500|0\n",                         /* group too high             */
        "1|City Hospital|-1|100|1500|0\n",                        /* group negative             */
        "1|City Hospital|2|100|1500|-1\n",                        /* status below range         */
        "1|City Hospital|2|100|1500|3\n",                         /* status above range         */
        "malformed request\n",                                    /* not a record               */
        "1|City Hospital|2|100|1500|0\n"
        "malformed request\n",                                    /* damage after a good record */
    };
    for (const char *content : bad_requests) {
        write_file("requests.txt", content);
        EXPECT_EQ(load_result(), 0) << content;
    }
    clean();

    /* ---------- users ---------- */
    write_file("users.txt",
               "staff|0102030405060708090a0b0c0d0e0f10|" + std::string(HASH_SIZE * 2, 'f') + "\n"
               "admin|" + hex_zeros(SALT_SIZE) + "|" + hex_zeros(HASH_SIZE) + "\n");
    {
        Database db = {};
        ASSERT_EQ(try_load(&db), 1);
        ASSERT_NE(db.users, nullptr);
        ASSERT_NE(db.users->next, nullptr);
        EXPECT_EQ(db.users->salt[0], 0x01U);
        EXPECT_EQ(db.users->salt[SALT_SIZE - 1], 0x10U);
        EXPECT_EQ(db.users->hash[HASH_SIZE - 1], 0xffU);
        EXPECT_STREQ(db.users->next->username, "admin");
        free_database(&db);
    }
    clean();

    const std::string bad_users[] = {
        "staff|00|00\n",                                          /* hex strings too short      */
        "staff|" + hex_zeros(SALT_SIZE) + "|00\n",                /* hash too short             */
        "staff|00|" + hex_zeros(HASH_SIZE) + "\n",                /* salt too short             */
        GOOD_USER + GOOD_USER,                                    /* duplicate username         */
        "this is certainly not a user record\n",                  /* not a record (20+ chars)   */
        GOOD_USER + "this is certainly not a user record\n",      /* damage after a good record */
    };
    for (const std::string &content : bad_users) {
        write_file("users.txt", content);
        EXPECT_EQ(load_result(), 0) << content;
    }
    clean();

    /* ---------- a file that exists but cannot be opened (not just "missing") ---------- */
    const char *const names[] = {"donors.txt", "batches.txt", "requests.txt", "users.txt"};
    for (const char *name : names) {
        make_unreadable(name);
        Database db = {};
        EXPECT_EQ(try_load(&db, &err), 0) << name;
        EXPECT_NE(err.find(std::string("cannot read ") + name), std::string::npos) << name;
        free_database(&db);
        clean();
    }

    /* ---------- out of memory while building each list ---------- */
    write_file("donors.txt", GOOD_DONOR);
    {
        Database db = {};
        EXPECT_EQ(try_load_no_memory(&db, sizeof(Donor)), 0);
        EXPECT_EQ(db.donors, nullptr);
        free_database(&db);
    }
    clean();
    write_file("batches.txt", GOOD_BATCH);
    {
        Database db = {};
        EXPECT_EQ(try_load_no_memory(&db, sizeof(Batch)), 0);
        EXPECT_EQ(db.batches, nullptr);
        free_database(&db);
    }
    clean();
    write_file("requests.txt", GOOD_REQUEST);
    {
        Database db = {};
        EXPECT_EQ(try_load_no_memory(&db, sizeof(Request)), 0);
        EXPECT_EQ(db.requests, nullptr);
        free_database(&db);
    }
    clean();
    write_file("users.txt", GOOD_USER);
    {
        Database db = {};
        EXPECT_EQ(try_load_no_memory(&db, sizeof(User)), 0);
        EXPECT_EQ(db.users, nullptr);
        free_database(&db);
    }
    clean();
}
