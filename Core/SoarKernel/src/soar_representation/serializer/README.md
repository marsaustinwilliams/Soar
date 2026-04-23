# Agent State Serializer Layout

`agent_state_serializer.cpp` remains the owning translation unit. The `.inl` files under this directory are include-backed implementation slices, kept in dependency order inside that translation unit to reduce refactor risk while making the serializer easier to navigate.

## File Map

- `agent_state_serializer_core_helpers.inl`: low-level shared helpers for WME lookup, counting, preference-key generation, and assertion-key generation.
- `agent_state_serializer_saved_conditions.inl`: saved-condition matching and synthetic condition-instantiation helpers.
- `agent_state_serializer_chunking.inl`: chunking runtime export/restore, active-goal state, pending assertions, and slot OSK chunking helpers.
- `agent_state_serializer_pending_restore.inl`: restore-side preference repair and pending IE/PE assertion reconstruction.
- `agent_state_serializer_identity_wmes.inl`: goal identity-set restoration plus WME owner/debug helpers.
- `agent_state_serializer_runtime.inl`: transient runtime reset, assertion cleanup, slot tracking reset, and phase/runtime finalization.
- `agent_state_serializer_symbols.inl`: symbol-map reconstruction and identity lookup/create helpers.
- `agent_state_serializer_preferences.inl`: restored preference identity binding and instantiation registration helpers.
- `agent_state_serializer_restore_build.inl`: goal-chain rebuilding, IO/module WME rebuilding, and restored agent counter/symbol setup.
- `agent_state_serializer_restore_wmes.inl`: restored preference creation plus WME creation/removal from snapshot entries.
- `agent_state_serializer_instantiations.inl`: refracted instantiation creation and restored preference reassignment.
- `agent_state_serializer_matches.inl`: live-match restoration, pending-match reification, and saved production firing-count helpers.
- `agent_state_serializer_export.inl`: snapshot export-side data collection and serialization helpers.
- `agent_state_serializer_rete.inl`: embedded rete import/export helpers.
- `agent_state_serializer_api.inl`: public serializer entry points included outside the anonymous namespace.

## Maintenance Notes

- Keep `.inl` files dependency-light. If a function only needs forward declarations already present in `agent_state_serializer.cpp`, prefer that over adding new headers.
- Prefer moving cohesive helper clusters, not interleaved fragments. The goal is readability without changing translation-unit semantics.
- Keep validation focused on `ctest --output-on-failure -R test_agent_state_cli` after each structural move, and use broader suites separately when doing behavior work.