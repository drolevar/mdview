function(mdview_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /EHsc
            /utf-8
            /Zc:__cplusplus
            /Zc:preprocessor)
        target_compile_definitions(${target} PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX)
    endif()
endfunction()
