# Settings modules

The Settings host loads a module without knowing how that module stores its page. A new module is an add-on. It does not require a change to the Settings window.

API version is `kSettingsApiVersion` in `SettingsModule.h`. It is 1. The host loads a module only when `settings_api_version()` returns that number. A module built for another version is skipped and reported in the syslog.

## Files

Put the module in its own directory and build it with the `Addon` rule. Install the binary in an add-ons directory named `Settings`:

- `/boot/system/add-ons/Settings`
- `/boot/system/non-packaged/add-ons/Settings`
- `/boot/home/config/add-ons/Settings`
- `/boot/home/config/non-packaged/add-ons/Settings`

The Appearance module is installed as `settings_appearance` in the system add-ons directory.

## Entry points

Both symbols are `extern "C"` and exported. They must not be inlined.

```cpp
extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}

extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new ExampleModule();
}
```

Include `SettingsModule.h` only. Do not include the Settings window or the host.

## Metadata

`Identifier()` is a stable id: letters, digits, `.`, `_`, and `-`, at most 64 characters. It is the file name under `~/config/settings/r5-settings/`. Two modules with the same id cannot both load.

`Category()` is a short id. The host already names `personalization`, `system`, `devices`, `network`, and `accounts`. Any other id is still listed, under that id as its label.

`Name()`, `Description()`, and `Keywords()` are what search compares. Search is case-insensitive.

`RequiresAdmin()` means Save is refused unless `SettingsContext::AdminChangeAllowed()` is true. That call is the existing administrator lock (`r5_admin_change_allowed`). A checkbox in the module is not the check. `SaveSettings()` must call `AdminChangeAllowed()` again before it writes a privileged value. Appearance does not require an administrator.

`CanSave()` is false for a page that only opens an existing preference program. Those programs keep their own files.

## The view

`CreateView()` returns a `BView`. The Settings window owns it and deletes it. The module must not delete it. Keep a non-owning pointer if the page has to update after Revert, and clear that pointer from the view's destructor.

The window does not rebuild itself when the page changes. It keeps the view and shows it again.

Call `NotifyChanged()` after the user edits a value. Do not write the file from the control handler. The window calls `SaveSettings()` or `RevertSettings()`.

## Storage

`ReadSettings()` / `WriteSettings()` flatten one `BMessage` into `~/config/settings/r5-settings/<identifier>`. A missing file is `B_ENTRY_NOT_FOUND`; treat that as the defaults.

Do not write Haiku's existing preference files from a new module. Open those programs with `LaunchLegacy(signature)` until the page is a real module.

`LoadSettings()` copies the file into the module. `SaveSettings()` writes it and clears the modified flag. `RevertSettings()` puts back the last loaded or saved values and updates the view.

## Failure

Return a `status_t`. The window shows it. If `CreateView()` returns `NULL`, the window shows a short error and the rest of Settings stays open.

A module that fails to load does not stop the others. The syslog line starts with `settings:`.
