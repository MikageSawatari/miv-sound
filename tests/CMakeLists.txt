add_executable(effetune_vst_unit_tests unit/engine_tests.cpp)
target_link_libraries(effetune_vst_unit_tests PRIVATE effetune_engine)
target_compile_features(effetune_vst_unit_tests PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(effetune_vst_unit_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_vst_unit_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME effetune_vst_unit_tests COMMAND effetune_vst_unit_tests)

add_executable(miv_startup_config_tests unit/config_store_startup_tests.cpp)
target_link_libraries(miv_startup_config_tests PRIVATE effetune_engine)
target_compile_features(miv_startup_config_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(miv_startup_config_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(miv_startup_config_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME miv_startup_config_tests COMMAND miv_startup_config_tests)

add_executable(effetune_upstream_integration_tests unit/upstream_integration_tests.cpp)
target_link_libraries(effetune_upstream_integration_tests PRIVATE effetune_engine)
target_include_directories(effetune_upstream_integration_tests PRIVATE
  "${EFFETUNE_UPSTREAM_SOURCE_DIR}/dsp/core"
  "${EFFETUNE_UPSTREAM_SOURCE_DIR}/dsp/generated/cpp")
target_compile_features(effetune_upstream_integration_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_upstream_integration_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_upstream_integration_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME effetune_upstream_integration_tests COMMAND effetune_upstream_integration_tests)
set_tests_properties(effetune_upstream_integration_tests PROPERTIES TIMEOUT 600)

add_executable(effetune_automation_binding_tests unit/automation_binding_tests.cpp)
target_link_libraries(effetune_automation_binding_tests PRIVATE effetune_engine)
target_compile_features(effetune_automation_binding_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_automation_binding_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_automation_binding_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME effetune_automation_binding_tests COMMAND effetune_automation_binding_tests)

add_executable(effetune_automation_scheduler_tests
  unit/automation_scheduler_tests.cpp)
target_link_libraries(effetune_automation_scheduler_tests PRIVATE effetune_engine)
target_compile_features(effetune_automation_scheduler_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_automation_scheduler_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_automation_scheduler_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME effetune_automation_scheduler_tests
  COMMAND effetune_automation_scheduler_tests)

add_executable(effetune_engine_batch_tests unit/engine_batch_tests.cpp)
target_link_libraries(effetune_engine_batch_tests PRIVATE effetune_engine)
target_compile_features(effetune_engine_batch_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_engine_batch_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_engine_batch_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME effetune_engine_batch_tests COMMAND effetune_engine_batch_tests)

add_executable(effetune_automation_parameter_bank_tests
  unit/automation_parameter_bank_tests.cpp
  ${PROJECT_SOURCE_DIR}/src/plugin/automation_parameters.cpp)
target_link_libraries(effetune_automation_parameter_bank_tests PRIVATE sdk effetune_engine)
if(APPLE)
  # The SDK's string and UUID helpers use CoreFoundation on macOS.
  target_link_libraries(effetune_automation_parameter_bank_tests PRIVATE
    "-framework CoreFoundation")
endif()
target_include_directories(effetune_automation_parameter_bank_tests PRIVATE
  ${PROJECT_SOURCE_DIR}/src)
target_compile_features(effetune_automation_parameter_bank_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_automation_parameter_bank_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_automation_parameter_bank_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME effetune_automation_parameter_bank_tests
  COMMAND effetune_automation_parameter_bank_tests)

add_executable(effetune_resampler_tests audio/resampler_tests.cpp)
target_link_libraries(effetune_resampler_tests PRIVATE effetune_engine)
target_compile_features(effetune_resampler_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_resampler_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_resampler_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME effetune_resampler_tests COMMAND effetune_resampler_tests)

add_executable(effetune_file_exchange_tests
  unit/file_exchange_tests.cpp
  ${PROJECT_SOURCE_DIR}/src/plugin/native_file_dialog.cpp
  ${PROJECT_SOURCE_DIR}/src/plugin/native_file_dialog.h)
target_include_directories(effetune_file_exchange_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(effetune_file_exchange_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_file_exchange_tests PRIVATE /W4 /WX /utf-8)
  target_link_libraries(effetune_file_exchange_tests PRIVATE ole32)
else()
  target_compile_options(effetune_file_exchange_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  if(APPLE)
    target_link_libraries(effetune_file_exchange_tests PRIVATE "-framework Cocoa")
  endif()
endif()
add_test(NAME effetune_file_exchange_tests COMMAND effetune_file_exchange_tests)

add_executable(effetune_json_storage_compatibility_tests unit/json_storage_compatibility_tests.cpp)
target_link_libraries(effetune_json_storage_compatibility_tests PRIVATE effetune_engine)
target_compile_features(effetune_json_storage_compatibility_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_json_storage_compatibility_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_json_storage_compatibility_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME effetune_json_storage_compatibility_tests
  COMMAND effetune_json_storage_compatibility_tests "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/json-storage")
set_tests_properties(effetune_json_storage_compatibility_tests PROPERTIES TIMEOUT 120)

if(WIN32 AND TARGET effetune_plugin)
  add_executable(miv_webview_permission_tests unit/webview_permission_tests.cpp)
  target_link_libraries(miv_webview_permission_tests PRIVATE effetune_choc user32 ole32 shell32 shlwapi)
  target_include_directories(miv_webview_permission_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(miv_webview_permission_tests PRIVATE cxx_std_20)
  target_compile_options(miv_webview_permission_tests PRIVATE /W4 /WX /utf-8)
  add_test(NAME miv_webview_permission_tests COMMAND miv_webview_permission_tests)

  # Link a WebViewHost double, never the WebView2 implementation or product DLL.
  add_executable(miv_processor_startup_tests
    unit/processor_startup_tests.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/automation_parameters.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/external_url.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/native_file_dialog.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/plugin_processor.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/plugin_view.cpp)
  target_link_libraries(miv_processor_startup_tests PRIVATE
    sdk effetune_engine user32 ole32 shell32 shlwapi)
  target_include_directories(miv_processor_startup_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${EFFETUNE_UPSTREAM_SOURCE_DIR}/dsp/core)
  target_compile_features(miv_processor_startup_tests PRIVATE cxx_std_20)
  target_compile_options(miv_processor_startup_tests PRIVATE /W4 /WX /utf-8)
  add_test(NAME miv_processor_startup_tests COMMAND miv_processor_startup_tests)
  set_tests_properties(miv_processor_startup_tests PROPERTIES TIMEOUT 120)

  add_executable(effetune_webview_test_lifetime_tests unit/webview_test_lifetime_tests.cpp)
  target_compile_features(effetune_webview_test_lifetime_tests PRIVATE cxx_std_20)
  target_compile_options(effetune_webview_test_lifetime_tests PRIVATE /W4 /WX /utf-8)
  target_link_libraries(effetune_webview_test_lifetime_tests PRIVATE shell32 user32)
  add_dependencies(effetune_webview_test_lifetime_tests effetune_plugin)
  add_test(NAME effetune_webview_test_lifetime_tests
    COMMAND effetune_webview_test_lifetime_tests "$<TARGET_FILE:effetune_plugin>")
  set_tests_properties(effetune_webview_test_lifetime_tests PROPERTIES TIMEOUT 120)
  add_test(NAME effetune_choc_webview_header_contract
    COMMAND "${EFFETUNE_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/esm/choc-webview-header.test.mjs")
  set_tests_properties(effetune_choc_webview_header_contract PROPERTIES TIMEOUT 120)
  add_executable(miv_sound_tests unit/miv_sound_tests.cpp)
  target_link_libraries(miv_sound_tests PRIVATE sdk effetune_engine version)
  target_compile_features(miv_sound_tests PRIVATE cxx_std_20)
  target_compile_options(miv_sound_tests PRIVATE /W4 /WX /utf-8)
  add_dependencies(miv_sound_tests effetune_plugin)
  add_test(NAME miv_sound_tests COMMAND miv_sound_tests "$<TARGET_FILE:effetune_plugin>"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/miv-mixwright-state.json")
  add_executable(effetune_now_playing_metadata_tests
    unit/now_playing_metadata_tests.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/automation_parameters.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/external_url.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/native_file_dialog.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/plugin_processor.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/plugin_view.cpp
    ${PROJECT_SOURCE_DIR}/src/bridge/webview_host.cpp)
  target_link_libraries(effetune_now_playing_metadata_tests PRIVATE
    sdk sdk_hosting effetune_engine user32 ole32 shell32 shlwapi)
  target_include_directories(effetune_now_playing_metadata_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${EFFETUNE_UPSTREAM_SOURCE_DIR}/dsp/core)
  target_compile_features(effetune_now_playing_metadata_tests PRIVATE cxx_std_20)
  target_compile_options(effetune_now_playing_metadata_tests PRIVATE /W4 /WX /utf-8)
  add_test(NAME effetune_now_playing_metadata_tests
    COMMAND effetune_now_playing_metadata_tests)
  set_tests_properties(effetune_now_playing_metadata_tests PROPERTIES TIMEOUT 120)

  add_executable(effetune_plugin_processing_lifecycle_tests
    unit/plugin_processing_lifecycle_tests.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/automation_parameters.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/external_url.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/native_file_dialog.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/plugin_processor.cpp
    ${PROJECT_SOURCE_DIR}/src/plugin/plugin_view.cpp
    ${PROJECT_SOURCE_DIR}/src/bridge/webview_host.cpp)
  target_link_libraries(effetune_plugin_processing_lifecycle_tests PRIVATE
    sdk effetune_engine user32 ole32 shell32 shlwapi)
  target_include_directories(effetune_plugin_processing_lifecycle_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${EFFETUNE_UPSTREAM_SOURCE_DIR}/dsp/core)
  target_compile_features(effetune_plugin_processing_lifecycle_tests PRIVATE cxx_std_20)
  target_compile_definitions(effetune_plugin_processing_lifecycle_tests PRIVATE
    EFFETUNE_PROCESSOR_TEST_HOOKS=1)
  target_compile_options(effetune_plugin_processing_lifecycle_tests PRIVATE /W4 /WX /utf-8)
  add_test(NAME effetune_plugin_processing_lifecycle_tests
    COMMAND effetune_plugin_processing_lifecycle_tests)

  add_executable(effetune_plugin_view_scaling_tests
    unit/plugin_view_scaling_tests.cpp)
  target_link_libraries(effetune_plugin_view_scaling_tests PRIVATE sdk)
  target_include_directories(effetune_plugin_view_scaling_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(effetune_plugin_view_scaling_tests PRIVATE cxx_std_20)
  target_compile_options(effetune_plugin_view_scaling_tests PRIVATE /W4 /WX /utf-8)
  add_dependencies(effetune_plugin_view_scaling_tests effetune_plugin)
  add_test(NAME effetune_plugin_view_scaling_tests
    COMMAND effetune_plugin_view_scaling_tests $<TARGET_FILE:effetune_plugin>)
endif()

if(WIN32 AND TARGET effetune_ui_assets)
  add_library(effetune_webview_loader_fixture SHARED
    webview/webview_loader_fixture.cpp
    webview/webview_loader_fixture.def)
  target_link_libraries(effetune_webview_loader_fixture PRIVATE effetune_choc)
  target_compile_features(effetune_webview_loader_fixture PRIVATE cxx_std_20)
  target_compile_options(effetune_webview_loader_fixture PRIVATE /W4 /WX /utf-8)

  add_executable(effetune_webview_initialisation_tests
    webview/webview_initialisation_tests.cpp)
  target_link_libraries(effetune_webview_initialisation_tests PRIVATE
    effetune_choc user32 ole32 shell32 shlwapi)
  target_compile_features(effetune_webview_initialisation_tests PRIVATE cxx_std_20)
  target_include_directories(effetune_webview_initialisation_tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_compile_options(effetune_webview_initialisation_tests PRIVATE /W4 /WX /utf-8)
  target_compile_definitions(effetune_webview_initialisation_tests PRIVATE
    EFFETUNE_WEBVIEW_LOADER_FIXTURE="$<TARGET_FILE:effetune_webview_loader_fixture>")
  add_dependencies(effetune_webview_initialisation_tests effetune_webview_loader_fixture)
  add_test(NAME effetune_webview_initialisation_tests COMMAND effetune_webview_initialisation_tests)
  set_tests_properties(effetune_webview_initialisation_tests PROPERTIES TIMEOUT 120)

  add_executable(effetune_webview_smoke
    webview/webview_smoke.cpp
    ${PROJECT_SOURCE_DIR}/src/bridge/webview_host.cpp
    ${PROJECT_SOURCE_DIR}/src/bridge/webview_host.h)
  target_link_libraries(effetune_webview_smoke PRIVATE effetune_engine effetune_choc user32 ole32 shell32 shlwapi)
  target_include_directories(effetune_webview_smoke PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(effetune_webview_smoke PRIVATE cxx_std_20)
  target_compile_definitions(effetune_webview_smoke PRIVATE
    EFFETUNE_WEBVIEW_ASSET_DIR="${CMAKE_BINARY_DIR}/webview-assets")
  target_compile_options(effetune_webview_smoke PRIVATE /W4 /WX /utf-8)
  add_dependencies(effetune_webview_smoke effetune_ui_assets)
  add_test(NAME effetune_webview_smoke COMMAND effetune_webview_smoke)
  # Local runs have taken under four minutes, including concurrent analysis.
  # Leave at least ten times that upper bound, including child-process cleanup.
  set_tests_properties(effetune_webview_smoke PROPERTIES TIMEOUT 2400)
endif()

if(TARGET effetune_ui_assets)
  add_test(NAME miv_original_visualizer_tests
    COMMAND "${EFFETUNE_NODE_EXECUTABLE}" --test
            "${PROJECT_SOURCE_DIR}/tests/esm/miv-original-visualizer.test.mjs")
  add_test(NAME effetune_json_storage_ui_compatibility
    COMMAND "${EFFETUNE_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/esm/json-storage-compatibility.test.mjs"
      --assets "${CMAKE_BINARY_DIR}/webview-assets" --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/json-storage")
  set_tests_properties(effetune_json_storage_ui_compatibility PROPERTIES TIMEOUT 120)
  add_test(NAME effetune_vst_upstream_ui_tests
    COMMAND "${EFFETUNE_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/esm/vst-upstream-ui.test.mjs"
            --assets "${CMAKE_BINARY_DIR}/webview-assets")
  add_test(NAME effetune_vst_audio_manager_tests
    COMMAND "${EFFETUNE_NODE_EXECUTABLE}" --test
            "${PROJECT_SOURCE_DIR}/tests/esm/vst-audio-manager.test.mjs")
  add_test(NAME effetune_ui_asset_contract
    COMMAND "${EFFETUNE_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/verify-ui-assets.mjs"
            --assets "${CMAKE_BINARY_DIR}/webview-assets"
            --source "${EFFETUNE_UPSTREAM_SOURCE_DIR}")
endif()

if(TARGET effetune_plugin_bundle_assets)
  add_test(NAME effetune_vst3_ui_asset_contract
    COMMAND "${EFFETUNE_NODE_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/verify-ui-assets.mjs"
            --assets
            "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/${EFFETUNE_PLUGIN_PACKAGE_NAME}.vst3/Contents/Resources/webview"
            --source "${EFFETUNE_UPSTREAM_SOURCE_DIR}")
endif()

if(APPLE AND TARGET effetune_plugin)
  add_test(NAME effetune_macos_bundle_info
    COMMAND "${CMAKE_COMMAND}"
            "-DBUNDLE_PATH=$<TARGET_BUNDLE_DIR:effetune_plugin>"
            "-DEXPECTED_EXECUTABLE=${EFFETUNE_PLUGIN_PACKAGE_NAME}"
            "-DEXPECTED_IDENTIFIER=${EFFETUNE_PLUGIN_BUNDLE_IDENTIFIER}"
            "-DEXPECTED_VERSION=${PROJECT_VERSION}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/verify_macos_bundle_info.cmake")
endif()

if(WIN32 AND TARGET validator AND TARGET effetune_plugin)
  # Give the SDK validator our test boundary. Rename its entry point and adapt
  # its module destructor below, keeping the SDK checkout unchanged.
  set_property(SOURCE
    "${vst3sdk_SOURCE_DIR}/public.sdk/samples/vst-hosting/validator/source/main.cpp"
    TARGET_DIRECTORY validator APPEND PROPERTY COMPILE_DEFINITIONS
    wmain=mivSoundValidatorMain)
  target_sources(validator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/support/validator_main.cpp")
  target_compile_features(validator PRIVATE cxx_std_20)
  target_link_libraries(validator PRIVATE shell32)
  # Drain on the module-owning STA BEFORE the SDK calls ExitDll/FreeLibrary.
  # Adapt a build-tree copy; keep the pinned SDK checkout untouched.
  set(effetune_validator_module "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp")
  set(effetune_validator_module_generator "${PROJECT_SOURCE_DIR}/tools/build-validator-module.mjs")
  set(effetune_validator_module_copy "${CMAKE_CURRENT_BINARY_DIR}/validator-module/module_win32.cpp")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${effetune_validator_module}" "${effetune_validator_module_generator}")
  execute_process(COMMAND "${EFFETUNE_NODE_EXECUTABLE}" "${effetune_validator_module_generator}"
    --source "${effetune_validator_module}" --out "${effetune_validator_module_copy}"
    --support "${CMAKE_CURRENT_SOURCE_DIR}/support/webview_test_lifetime.h"
    RESULT_VARIABLE effetune_validator_module_result)
  if(NOT effetune_validator_module_result EQUAL 0)
    message(FATAL_ERROR "Preparing the validator module lifetime adapter failed")
  endif()
  get_target_property(effetune_validator_sources validator SOURCES)
  get_target_property(effetune_validator_source_dir validator SOURCE_DIR)
  set(effetune_validator_adapted_sources)
  set(effetune_validator_module_matches 0)
  foreach(effetune_validator_source IN LISTS effetune_validator_sources)
    if(NOT IS_ABSOLUTE "${effetune_validator_source}")
      set(effetune_validator_source "${effetune_validator_source_dir}/${effetune_validator_source}")
    endif()
    if(effetune_validator_source STREQUAL effetune_validator_module)
      math(EXPR effetune_validator_module_matches "${effetune_validator_module_matches} + 1")
      list(APPEND effetune_validator_adapted_sources "${effetune_validator_module_copy}")
    else()
      list(APPEND effetune_validator_adapted_sources "${effetune_validator_source}")
    endif()
  endforeach()
  if(NOT effetune_validator_module_matches EQUAL 1)
    message(FATAL_ERROR "Expected exactly one SDK validator Windows module source")
  endif()
  set_property(TARGET validator PROPERTY SOURCES "${effetune_validator_adapted_sources}")
  target_include_directories(validator PRIVATE "${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting")
  add_test(NAME effetune_vst3_validator
    COMMAND $<TARGET_FILE:validator>
            "${CMAKE_BINARY_DIR}/VST3/$<CONFIG>/${EFFETUNE_PLUGIN_PACKAGE_NAME}.vst3")
  set_tests_properties(effetune_vst3_validator PROPERTIES TIMEOUT 120)
elseif(APPLE AND TARGET validator AND TARGET effetune_plugin)
  add_test(NAME effetune_vst3_validator
    COMMAND $<TARGET_FILE:validator> $<TARGET_BUNDLE_DIR:effetune_plugin>)
  set_tests_properties(effetune_vst3_validator PROPERTIES TIMEOUT 120)
endif()

add_executable(effetune_sfz_library_tests unit/sfz_library_tests.cpp)
target_link_libraries(effetune_sfz_library_tests PRIVATE effetune_engine)
target_compile_features(effetune_sfz_library_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(effetune_sfz_library_tests PRIVATE /W4 /WX /utf-8)
else()
  target_compile_options(effetune_sfz_library_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME effetune_sfz_library_tests COMMAND effetune_sfz_library_tests)
set_tests_properties(effetune_sfz_library_tests PROPERTIES TIMEOUT 1200)

# Every test gets its own disposable build-tree storage, including validators
# and child-process smoke fixtures. None may inherit the user's data directory.
get_property(effetune_registered_tests DIRECTORY PROPERTY TESTS)
foreach(effetune_test IN LISTS effetune_registered_tests)
  set_property(TEST "${effetune_test}" APPEND PROPERTY ENVIRONMENT
    "MIV_SOUND_DATA_DIR=${CMAKE_CURRENT_BINARY_DIR}/miv-sound-test-data/${effetune_test}")
endforeach()

if(WIN32 AND TARGET effetune_plugin)
  # Last resort for a broken run whose callbacks/processes never finish.
  # Normal teardown keeps pumping after a failure until the browsers exit.
  set_tests_properties(effetune_webview_smoke PROPERTIES TIMEOUT 2400)
  set_tests_properties(effetune_now_playing_metadata_tests effetune_vst3_validator PROPERTIES TIMEOUT 120)
  set_tests_properties(
    effetune_plugin_processing_lifecycle_tests effetune_plugin_view_scaling_tests
    miv_sound_tests PROPERTIES TIMEOUT 300)
endif()
