if(NOT DEFINED THREADSCHEDULE_SOURCE_DIR)
    message(FATAL_ERROR "THREADSCHEDULE_SOURCE_DIR is required")
endif()

set(include_root "${THREADSCHEDULE_SOURCE_DIR}/include/threadschedule")

function(check_layer layer forbidden)
    set(files ${ARGN})
    foreach(file IN LISTS files)
        file(READ "${file}" contents)
        string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]*[>\"]" includes "${contents}")
        foreach(include IN LISTS includes)
            if(include MATCHES "${forbidden}")
                file(RELATIVE_PATH relative "${THREADSCHEDULE_SOURCE_DIR}" "${file}")
                message(FATAL_ERROR "${layer} layer violation in ${relative}: ${include}")
            endif()
        endforeach()
    endforeach()
endfunction()

file(GLOB foundation_files
    "${include_root}/cpu_id.hpp"
    "${include_root}/nice_value.hpp"
    "${include_root}/pool_statistics.hpp"
    "${include_root}/realtime_priority.hpp"
    "${include_root}/result.hpp"
    "${include_root}/scheduling.hpp"
    "${include_root}/thread_affinity.hpp"
    "${include_root}/thread_config.hpp"
    "${include_root}/thread_id.hpp"
    "${include_root}/worker_count.hpp"
    "${include_root}/worker_registration.hpp"
    "${include_root}/detail/callable/*.hpp"
    "${include_root}/detail/scope_exit.hpp"
    "${include_root}/detail/try_result.hpp"
    "${include_root}/detail/unique_handle.hpp"
)
check_layer("foundation"
    "detail/(thread|registry|pool|scheduled)/|(thread|thread_registry|thread_pool|scheduled_pool|scheduled_task)\\.hpp"
    ${foundation_files}
)

file(GLOB thread_files "${include_root}/detail/thread/*.hpp" "${include_root}/detail/thread_backend.hpp")
list(APPEND thread_files
    "${include_root}/thread.hpp"
    "${include_root}/jthread.hpp"
    "${include_root}/thread_view.hpp"
    "${include_root}/this_thread.hpp"
)
check_layer("thread"
    "detail/(registry|pool|scheduled)/|(thread_registry|thread_pool|scheduled_pool|scheduled_task)\\.hpp"
    ${thread_files}
)

file(GLOB registry_files "${include_root}/detail/registry/*.hpp")
list(APPEND registry_files "${include_root}/thread_registry.hpp")
check_layer("registry" "detail/(pool|scheduled)/|(thread_pool|scheduled_pool|scheduled_task)\\.hpp" ${registry_files})

file(GLOB pool_files "${include_root}/detail/pool/*.hpp")
list(APPEND pool_files "${include_root}/thread_pool.hpp")
check_layer("pool" "detail/scheduled/|scheduled_(pool|task)\\.hpp" ${pool_files})

# Umbrellas are consumer conveniences. Individual library headers must name
# their direct dependencies so include relationships remain reviewable.
file(GLOB_RECURSE focused_public_headers "${include_root}/*.hpp")
list(REMOVE_ITEM focused_public_headers
    "${include_root}/advanced.hpp"
    "${include_root}/core.hpp"
    "${include_root}/threadschedule.hpp"
    "${include_root}/advanced/pools.hpp"
)
check_layer("focused public header" "[/\"](advanced|core|pools|threadschedule)\\.hpp" ${focused_public_headers})
