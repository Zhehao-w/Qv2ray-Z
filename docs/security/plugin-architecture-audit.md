# Plugin architecture and attack-surface audit

## Scope

This audit documents the maintained Qv2ray-Z plugin boundary after external native plugin support was retired. It covers the active build graph, runtime registration, plugin-related UI/config remnants, and Windows packaging guardrails. It does not attempt a configuration-schema migration or a broad language/i18n cleanup.

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

The post-audit cleanup removed the dead plugin surfaces that could be proven unnecessary without changing maintained protocol/subscription behavior or configuration compatibility:

1. The inactive `src/plugins/utils/**` tree, including its historical `MODULE` target and platform plugin install rules, was removed from the source tree.
2. The retired `w_PluginManager` implementation and `.ui` form were removed from the build/source surface. The historical private MainWindow auto-connect slot still includes a tiny no-op tombstone header so this focused cleanup does not rewrite the large MainWindow implementation; invoking that legacy slot performs no UI, plugin-host, filesystem, settings, or executable-loading action.
3. The StyleManager polish hook that existed only to hide the already-absent `pluginsBtn` control was removed.

The first-party `QvPluginHost` dispatch path, protocol/subscription components, their settings widgets, and the compile-time plugin interface remain active and intentionally unchanged.

## Remaining compatibility surface

The following items remain deliberately out of this focused cleanup because they are active compile-time APIs, persistent compatibility surfaces, or harmless legacy symbols:

1. `QvPluginHost`, plugin-interface types, and several paths/APIs still use historical plugin naming even though their runtime role is now internal component dispatch.
2. `Qv2rayConfig_Plugin::pluginStates` remains for old configuration compatibility. `QvPluginHost` ignores legacy enable-state data for external plugins instead of restoring external loading.
3. `QV2RAY_PLUGIN_SETTINGS_DIR` remains as the settings location for the built-in components. Whether to rename/migrate that persistent path belongs to the later compatibility/schema audit.
4. The pinned `src/plugin-interface` dependency remains because protocol/subscription implementations still consume its active compile-time contracts.
5. The private `on_pluginsBtn_clicked` MainWindow slot name and its inert `w_PluginManager.hpp` tombstone remain only to avoid a high-churn rewrite of `w_MainWindow.cpp` in this attack-surface PR. They expose no plugin-management behavior and can be removed with other dormant MainWindow symbols in the later deep legacy cleanup.

Renaming or removing the active/persistent structures should be handled by the later compatibility/schema audit rather than folded into attack-surface cleanup.

## i18n/resource note

The active built-in component CMake files still append source lists to historical translation variables, but this audit found no plugin-specific executable resource loader or native extension mechanism associated with that naming. Broader language/i18n retirement remains outside this cleanup unless a later audit finds a direct executable-loading/security intersection.

## Security invariant going forward

Reintroducing runtime-loaded native plugins would create a new executable trust boundary and must not happen as an incidental refactor. It requires an explicit architecture/security decision, new threat modeling, packaging policy, validation, and user-facing support scope.
