#include <gtest/gtest.h>
 
extern "C"
{
#include "transaction.h"
}
 
TEST(TransactionTest, LoadTransactions)
{
    loadTransactionsFromFile();
 
    SUCCEED();
}
 
TEST(TransactionTest, DisplayTransactions)
{
    loadTransactionsFromFile();
 
    displayTransactions();
 
    SUCCEED();
}
 
TEST(TransactionTest, VerifyIntegrity)
{
    loadTransactionsFromFile();
 
    verifyIntegrity();
 
    SUCCEED();
}
 
TEST(TransactionTest, FreeTransactionList)
{
    loadTransactionsFromFile();
 
    freeTransactionList();
 
    SUCCEED();
}
 