set(CPM_VERSION 0.40.2)
set(CPM_DOWNLOAD_LOCATION
    "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_VERSION}.cmake")

if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
        "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_VERSION}/CPM.cmake"
        "${CPM_DOWNLOAD_LOCATION}"
        TLS_VERIFY ON
        STATUS cpm_download_status
    )
    list(GET cpm_download_status 0 cpm_download_result)
    if(NOT cpm_download_result EQUAL 0)
        message(FATAL_ERROR "Unable to download CPM.cmake: ${cpm_download_status}")
    endif()
endif()

include("${CPM_DOWNLOAD_LOCATION}")

CPMAddPackage(
    NAME MinHook
    GITHUB_REPOSITORY TsudaKageyu/minhook
    GIT_TAG v1.3.4
    EXCLUDE_FROM_ALL YES
    SYSTEM YES
)
