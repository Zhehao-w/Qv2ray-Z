# Plugin architecture and attack-surface audit

## Scope

This audit documents the maintained Qv2ray-Z plugin boundary after external native plugin support was retired. It covers the active build graph, runtime registration, plugin-related UI/config remnants, and Windows packaging guardrails. It does not attempt a broad language/i18n cleanup.

## Current trust boundary

The maintained application does not discover or load third-party native plugins at runtime.

- `src/components/plugins/QvPluginHost.cpp` constructs only two first-party components: `InternalProtocolSupportPlugin` and `InternalSubscriptionSupportPlugin`.
- `src/plugins/protocols/QvPlugin-BuiltinProtocolSupport.cmake` and `src/plugins/subscription-adapters/QvPlugin-BuiltinSubscriptionAdapters.cmake` build those components as `OBJECT` libraries and link their object files into `qv2ray_baselib`.
- `src/plugin-interface` remains a pinned compile-time interface dependency. It supplies shared interface/types and Qt interface declarations; it is not a runtime loader.
- `plugin_settings/` contains JSON settings for the built-in components. Reading those files is data/configuration loading, not executable-code loading.
- The authoritative Windows package workflow rejects deployed legacy `QvPlugin-*.dll` files.

The resulting maintained component model is therefore first-party and in-process: protocol serialization/editing and subscription parsing retain historical plugin vocabulary, but not an external DLL/shared-object execution boundary.

## Guard added by this audit

`cmake/plugin-boundary.cmake` is executed during normal CMake configuration. It fails configuration if:

- either maintained built-in component stops being an `OBJECT_LIBRARY`;
- the retired `QvPlugin-BuiltinUtils` target is reintroduced into the active graph; or
- `QvPluginHost.cpp` reintroduces a direct native-library loader marker such as `QPluginLoader`, `QLibrary`, `LoadLibrary`, or `dlopen`.

This is a focused architecture regression guard, not a general static-analysis claim. It deliberately checks the active plugin registration/build boundary and leaves unrelated platform library usage alone.

## Focused cleanup completed

The post-audit cleanup removed dead plugin surfaces that could be proven unnecessary without changing maintained protocol/subscription behavior:

1. The inactive `src/plugins/utils/**` tree, including its historical `MODULE` target and platform plugin install rules, was removed from the source tree.
2. The retired `w_PluginManager` implementation and `.ui` form were removed from the build/source surface together with the historical `open plugin` command branch and private `on_pluginsBtn_clicked` MainWindow slot. No Plugin Manager compatibility tombstone remains.
3. The StyleManager polish hook that existed only to hide the already-absent `pluginsBtn` control was removed.
4. The retired `pluginStates` enable/disable state is no longer part of the live configuration schema. QJsonStruct ignores unknown legacy keys, so existing config files remain readable while future saves stop persisting the dead field; a regression test covers that behavior.
5. Plugin Manager-only host display metadata/component-label helpers and the fixed enable/disable compatibility API were removed.
6. The built-in protocol component's empty settings form was removed. Its GUI interface now returns no settings widget while retaining all inbound/outbound editors.

The first-party `QvPluginHost` dispatch path, protocol/subscription components, their persisted component settings, and the compile-time plugin interface remain active.

## Remaining compatibility surface

The following items remain because they are active compile-time APIs or persistent compatibility surfaces:

1. `QvPluginHost`, plugin-interface types, and several paths/APIs still use historical plugin naming even though their runtime role is now internal component dispatch.
2. `QV2RAY_PLUGIN_SETTINGS_DIR` and `plugin_settings/` remain the settings location for the built-in components. Renaming that persistent path would require an explicit migration and is not part of this cleanup.
3. The pinned `src/plugin-interface` dependency remains because protocol/subscription implementations still consume its active compile-time contracts.
4. `Qv2rayConfig_Plugin` remains because `v2rayIntegration` and `portAllocationStart` are still active kernel settings despite the historical struct name.

Renaming active/persistent structures should be handled separately and only with an explicit compatibility plan.

## i18n/resource note

The active built-in component CMake files still append source lists to historical translation variables, but this audit found no plugin-specific executable resource loader or native extension mechanism associated with that naming. Broader language/i18n retirement remains outside this cleanup unless a later audit finds a direct executable-loading/security intersection.

## Security invariant going forward

Reintroducing runtime-loaded native plugins would create a new executable trust boundary and must not happen as an incidental refactor. It requires an explicit architecture/security decision, new threat modeling, packaging policy, validation, and user-facing support scope.
