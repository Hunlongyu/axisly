add_executable(gridly_contract_tests api_contract_tests.cpp)
target_link_libraries(gridly_contract_tests PRIVATE gridly::gridly)
set_target_properties(gridly_contract_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_contract_tests)

foreach(contract IN ITEMS geometry tracks invalid_tracks ranges invalid_ranges grids measurement results)
    add_test(NAME gridly.${contract} COMMAND gridly_contract_tests ${contract})
endforeach()

add_executable(gridly_fixed_tests fixed/fixed_tests.cpp)
target_link_libraries(gridly_fixed_tests PRIVATE gridly::gridly)
set_target_properties(gridly_fixed_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_fixed_tests)

foreach(behavior IN ITEMS
    one_cell multiple_columns multiple_rows asymmetric_padding gaps fractional
    zero_container zero_tracks empty_grids overflow_layout identity_and_order
    invalid_container invalid_geometry invalid_placement invalid_span
    invalid_item_values range_in_fixed_grid duplicate_ids duplicate_order duplicate_validation_order
    duplicate_randomized numeric_overflow deterministic)
    add_test(NAME gridly.fixed.${behavior} COMMAND gridly_fixed_tests ${behavior})
endforeach()

add_executable(gridly_weighted_tests weighted/weighted_tests.cpp)
target_link_libraries(gridly_weighted_tests PRIVATE gridly::gridly)
set_target_properties(gridly_weighted_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_weighted_tests)

foreach(behavior IN ITEMS
    equal_weights ratio_one_two ratio_one_two_three mixed_tracks both_axes
    padding_and_gaps no_free_space insufficient_space zero_container
    minimum_freeze maximum_freeze competing_bounds cascading_bounds
    all_capped equal_bounds empty_items numeric_limits extreme_weights
    weight_scale resize_and_determinism numeric_overflow reference_cases)
    add_test(NAME gridly.weighted.${behavior} COMMAND gridly_weighted_tests ${behavior})
endforeach()

add_executable(gridly_alignment_tests alignment/alignment_tests.cpp)
target_link_libraries(gridly_alignment_tests PRIVATE gridly::gridly)
set_target_properties(gridly_alignment_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_alignment_tests)

foreach(behavior IN ITEMS
    defaults start center end stretch all_axis_pairs margins asymmetric_margins
    oversized_preferred zero_preferred oversized_margins zero_cells fractional
    padding_and_gaps weighted_cells cell_rectangles invalid_preferred
    invalid_margins invalid_alignments extreme_geometry deterministic bounds_invariants)
    add_test(NAME gridly.alignment.${behavior} COMMAND gridly_alignment_tests ${behavior})
endforeach()

add_executable(gridly_measurement_tests measurement/measurement_tests.cpp)
target_link_libraries(gridly_measurement_tests PRIVATE gridly::gridly)
set_target_properties(gridly_measurement_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_measurement_tests)

foreach(behavior IN ITEMS
    empty_content static_fallback null_callback intrinsic_size largest_contribution
    content_shrink minimum_overflow rigid_content mixed_tracks weighted_minimum
    weighted_cap both_axes margins width_reflow alignment_width clipped_width
    zero_width advisory_space fixed_weighted_compatibility row_remeasurement
    callback_order invalid_intrinsic invalid_constrained invalid_order input_validation
    numeric_overflow large_preferred fractional_limits exceptions reentrant
    deterministic resize randomized_invariants)
    add_test(NAME gridly.measurement.${behavior} COMMAND gridly_measurement_tests ${behavior})
endforeach()

add_executable(gridly_spanning_tests spanning/spanning_tests.cpp)
target_link_libraries(gridly_spanning_tests PRIVATE gridly::gridly)
set_target_properties(gridly_spanning_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_spanning_tests)

foreach(behavior IN ITEMS
    fixed_columns fixed_rows both_axes subspan alignments oversized_margins
    overlapping_fixed content_preferred content_minimum existing_content
    fixed_content content_weighted weighted_minimum weighted_existing_minimum
    weighted_cap all_capped weighted_preferred overlapping_contributions
    duplicate_spans shorter_spans_first permutation width_reflow row_reflow
    aligned_measurement zero_width zero_tracks_with_gaps resize invalid_spans
    numeric_overflow huge_offset fractional_limits large_preferred extreme_weights
    independent_axes randomized_invariants)
    add_test(NAME gridly.spanning.${behavior} COMMAND gridly_spanning_tests ${behavior})
endforeach()

add_executable(gridly_range_tests range/range_tests.cpp)
target_link_libraries(gridly_range_tests PRIVATE gridly::gridly)
set_target_properties(gridly_range_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_range_tests)

foreach(behavior IN ITEMS
    fixed_fixed fixed_content fixed_weighted content_fixed content_content content_weighted
    empty_ranges equal_bounds fixed_floor_priority fixed_cap_priority margins_alignment padding_gaps
    mixed_growth multiple_items content_shrink span_capped_content span_cascade span_weighted_fallback
    span_all_capped span_dynamic_minimum both_axes row_remeasurement known_zero_width callback_validation
    numeric_overflow cap_prevents_overflow fractional_limits extreme_weights deterministic_resize
    compatibility_reference randomized_bounds)
    add_test(NAME gridly.range.${behavior} COMMAND gridly_range_tests ${behavior})
endforeach()

add_executable(gridly_nested_tests nested/nested_tests.cpp)
target_link_libraries(gridly_nested_tests PRIVATE gridly::gridly)
set_target_properties(gridly_nested_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_nested_tests)

foreach(behavior IN ITEMS
    lambda_capture lambda_move_only lambda_const_function lambda_raw_compatibility lambda_exceptions lambda_reentrant
    flat_equivalence fixed_child content_child width_reflow aligned_child scoped_shared_ids three_levels spanning_child range_child advisory_dependent_probes
    known_zero_width empty_grids overflowing_child invalid_root invalid_child invalid_measurement invalid_mapping duplicate_local_ids
    self_cycle indirect_cycle depth_limits work_limits final_failure exception_recovery numeric_overflow child_core_overflow fractional_offsets randomized_final_layout)
    add_test(NAME gridly.nested.${behavior} COMMAND gridly_nested_tests ${behavior})
endforeach()

add_executable(gridly_cache_tests cache/cache_tests.cpp)
target_link_libraries(gridly_cache_tests PRIVATE gridly::gridly)
set_target_properties(gridly_cache_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_cache_tests)

foreach(behavior IN ITEMS
    reuse snapshots unchanged_setters spec_changes item_changes item_sequence size_changes
    measurement_counts content_version invalidation callback_changes advisory_dimensions
    invalid_inputs invalid_measurement exceptions reentrant_mutation independent_cache
    empty_and_bounds randomized_updates span_growth_shrink prepared_validation_priority
    prepared_axis_transitions prepared_numeric_recovery prepared_second_pass_failure prepared_randomized_spans)
    add_test(NAME gridly.cache.${behavior} COMMAND gridly_cache_tests ${behavior})
endforeach()

# Compile every installed header independently to catch transitive include leaks.
add_executable(gridly_advanced_tests advanced/advanced_tests.cpp)
target_link_libraries(gridly_advanced_tests PRIVATE gridly::gridly)
set_target_properties(gridly_advanced_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_advanced_tests)
foreach(behavior IN ITEMS placement_row placement_column placement_locked placement_locked_spans implicit_tracks placement_limits placement_randomized
    percentage percentage_ranges baseline_static baseline_reflow baseline_spans_and_clipping baseline_extreme_caps rounding rounding_shared_edges
    measurement_keys measurement_lru measurement_failures grid_measurement_cache cache_exception_identity advanced_cache_equivalence)
    add_test(NAME gridly.advanced.${behavior} COMMAND gridly_advanced_tests ${behavior})
endforeach()

set(header_check_sources)
add_executable(gridly_tree_tests tree/tree_tests.cpp)
target_link_libraries(gridly_tree_tests PRIVATE gridly::gridly)
set_target_properties(gridly_tree_tests PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_tree_tests)
foreach(behavior IN ITEMS flat_equivalence unchanged targeted_dirty nested_reference sibling_subtree_reuse topology
    handles_and_validation error_atomicity callback_guards limits rounding_tree randomized_updates)
    add_test(NAME gridly.tree.${behavior} COMMAND gridly_tree_tests ${behavior})
endforeach()

foreach(header IN ITEMS geometry track grid layout measurement result cache rounding measurement_cache tree gridly)
    set(source "${CMAKE_CURRENT_BINARY_DIR}/header_${header}.cpp")
    file(WRITE "${source}" "#include <gridly/${header}.hpp>\n")
    list(APPEND header_check_sources "${source}")
endforeach()
add_library(gridly_header_checks OBJECT ${header_check_sources})
target_link_libraries(gridly_header_checks PRIVATE gridly::gridly)
set_target_properties(gridly_header_checks PROPERTIES CXX_EXTENSIONS OFF)
gridly_enable_warnings(gridly_header_checks)

if(GRIDLY_BUILD_REFERENCE_TESTS)
    add_subdirectory(reference)
endif()
