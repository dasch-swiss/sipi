// main.cpp:

// 1. Add these two lines to your main:
#define APPROVALS_GOOGLETEST_EXISTING_MAIN
#include "ApprovalTests.hpp"

// This puts "received" and "approved" files in approval_tests/ sub-directory,
// keeping the test source directory tidy:
auto directoryDisposer = ApprovalTests::Approvals::useApprovalsSubdirectory("approval_tests");

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  // 2. Add this line to your main:
  ApprovalTests::initializeApprovalTestsForGoogleTests();

  return RUN_ALL_TESTS();
}
