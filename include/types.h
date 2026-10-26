/* types.h - the limits, the status values and every structure the program uses.
   All the other header files include this one. */
#ifndef TYPES_H
#define TYPES_H


/* ---------- Limits and rules (change them here) ---------- */
#define NAME_LEN                50
#define PHONE_LEN               16
#define USERNAME_LEN            20
#define PASSWORD_LEN            64
#define GROUP_COUNT             8
#define MIN_DONATION_ML         200
#define MAX_DONATION_ML         600
#define MIN_REQUEST_ML          50
#define MAX_REQUEST_ML          20000
#define SHELF_LIFE_DAYS         35
#define MIN_AGE                 18
#define MAX_AGE                 65
#define DAYS_BETWEEN_DONATIONS  56
#define DONOR_INACTIVE_DAYS     180
#define DONOR_DELETION_DAYS     30
#define LOW_STOCK_ML            1000
#define SECONDS_PER_DAY         86400L
#define MAX_LOGIN_ATTEMPTS      3
#define SALT_SIZE               16
#define HASH_SIZE               32

/* ---------- Status values stored in the data files ---------- */
enum { BATCH_WAITING = 0, BATCH_AVAILABLE = 1, BATCH_DISCARDED = 2 };
enum { REQUEST_PENDING = 0, REQUEST_FULFILLED = 1, REQUEST_REJECTED = 2 };

/* ---------- Result of an operation ---------- */
typedef enum {
    RESULT_OK = 0,
    RESULT_NOT_FOUND,
    RESULT_EXISTS,
    RESULT_BAD_INPUT,
    RESULT_WRONG_STATE,
    RESULT_NOT_ENOUGH_STOCK,
    RESULT_NO_MEMORY,
    RESULT_FAILED
} Result;

typedef enum {
    DONOR_DEACTIVATE,
    DONOR_DELETE
} DonorAction;

typedef enum {
    BATCH_TEST,
    BATCH_DISCARD
} BatchAction;

typedef enum {
    REQUEST_FULFILL,
    REQUEST_REJECT
} RequestAction;

typedef enum {
    INPUT_TEXT,
    INPUT_PHONE,
    INPUT_NUMBER,
    INPUT_GROUP,
    INPUT_YES
} InputKind;

/* ---------- The records. Each one is a node of a linked list. ---------- */
typedef struct Donor {
    int id;
    char name[NAME_LEN];
    char phone[PHONE_LEN];
    int group;                /* 0..7, in the order listed in the input prompt */
    int birth_year;
    int active;               /* 1 = active, 0 = deactivated */
    long last_donation;       /* seconds since 1970, 0 = never donated */
    long registered_at;       /* inactivity clock for donors who never donated */
    long deactivated_at;      /* seconds since 1970, 0 = currently active */
    struct Donor *next;
} Donor;

/* A batch is one donation of blood (one bag). */
typedef struct Batch {
    int id;
    int donor_id;
    int group;
    int ml_collected;
    int ml_left;
    long collected;           /* when it was donated */
    long expiry;              /* last day it may be used */
    int status;               /* BATCH_WAITING, BATCH_AVAILABLE or BATCH_DISCARDED */
    struct Batch *next;
} Batch;

typedef struct Request {
    int id;
    char hospital[NAME_LEN];
    int group;
    int ml;
    long date;
    int status;               /* REQUEST_PENDING, REQUEST_FULFILLED or REQUEST_REJECTED */
    struct Request *next;
} Request;

typedef struct User {
    char username[USERNAME_LEN];
    unsigned char salt[SALT_SIZE];
    unsigned char hash[HASH_SIZE];     /* the password itself is never stored */
    struct User *next;
} User;

/* Everything the program knows: four linked lists. */
typedef struct {
    Donor *donors;
    Batch *batches;
    Request *requests;
    User *users;
} Database;

#endif
