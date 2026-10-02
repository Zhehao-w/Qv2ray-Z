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
- the legacy `QvPlugin-BuiltinUtils` target becomes active again; or
- `QvPluginHost.cpp` reintroduces a direct native-library loader marker such as `QPluginLoader`, `QLibrary`, `LoadLibrary`, or `dlopen`.

This is a focused architecture regression guard, not a general static-analysis claim. It deliberately checks the active plugin registration/build boundary and leaves unrelated platform library usage alone.

## Residual legacy surface

The audit found several remnants that are not currently an executable external-plugin path but should be removed in the focused cleanup PR:

1. `src/plugins/utils/QvPlugin-BuiltinUtils.cmake` still contains an old `MODULE` target and platform plugin install rules. The top-level build does not include this file, so the recipe is currently inert.
2. `src/plugins/utils/**` remains in the source tree even though the maintained build does not activate that component.
3. `w_PluginManager` is still compiled, and `MainWindow::on_pluginsBtn_clicked()` can construct it, while the normal `pluginsBtn` entry point is hidden by `StyleManager`. The window now describes bundled components rather than managing third-party plugins, but the legacy UI shell is unnecessary.
4. `QvPluginHost`, plugin-interface types, and several paths/APIs still use historical plugin naming even though their runtime role is now internal component dispatch.
5. `Qv2rayConfig_Plugin::pluginStates` remains for old configuration compatibility. `QvPluginHost` ignores legacy enable-state data for external plugins instead of restoring external loading.
6. `QV2RAY_PLUGIN_SETTINGS_DIR` remains as the settings location for the built-in components. Whether to rename/migrate that persistent path belongs to the later compatibility/schema audit, not this cleanup sequence.

## Focused cleanup boundary

The next plugin-focused cleanup PR should remove only remnants whose runtime/build dependencies can be proven dead without changing protocol, subscription, routing, system-proxy, or configuration compatibility behavior. The primary candidates are the inactive built-in-utils plugin tree/build recipe and the hidden Plugin Manager UI path. Any attempt to remove the pinned plugin-interface dependency or rename persistent plugin configuration/settings structures should be treated separately because those changes touch active compile-time APIs or backward compatibility.

## i18n/resource note

The active built-in component CMake files still append source lists to historical translation variables, but this audit found no plugin-specific executable resource loader or native extension mechanism associated with that naming. Broader language/i18n retirement remains outside this PR unless a later audit finds a direct executable-loading/security intersection.

## Security invariant going forward

Reintroducing runtime-loaded native plugins would create a new executable trust boundary and must not happen as an incidental refactor. It requires an explicit architecture/security decision, new threat modeling, packaging policy, validation, and user-facing support scope.
