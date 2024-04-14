include(FetchContent)
FetchContent_Declare(
        approvaltests
        GIT_REPOSITORY https://github.com/approvals/ApprovalTests.cpp.git
        GIT_TAG v.10.13.0  # You can specify a particular tag or commit if required
)
FetchContent_MakeAvailable(approvaltests)

add_dependencies(ApprovalTests googletest)
