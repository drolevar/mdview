function(mdview_configure_wlx_plugin target)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
            set(extension ".wlxa64")
        else()
            set(extension ".wlx64")
        endif()
    else()
        set(extension ".wlx")
    endif()

    set_target_properties(${target} PROPERTIES
        PREFIX ""
        SUFFIX ${extension}
        OUTPUT_NAME "mdview")
endfunction()
