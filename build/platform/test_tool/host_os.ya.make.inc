IF (HOST_OS_DARWIN AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:5530943794)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:5530943794)
ELSEIF (HOST_OS_DARWIN AND HOST_ARCH_ARM64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:5530943391)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:5530943391)
ELSEIF (HOST_OS_LINUX AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:5530945155)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:5530945155)
ELSEIF (HOST_OS_LINUX AND HOST_ARCH_AARCH64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:5530942800)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:5530942800)
ELSEIF (HOST_OS_WINDOWS AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:5530944487)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:5530944487)

ENDIF()