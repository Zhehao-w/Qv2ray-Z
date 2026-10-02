#pragma once

class QWidget;

// Compatibility tombstone for the retired Plugin Manager auto-connect slot.
// The MainWindow UI no longer exposes that control, and all plugin-management
// implementation/UI code has been removed. Keep this tiny no-op type only until
// the later legacy MainWindow-slot cleanup removes the historical slot symbol.
class PluginManageWindow final
{
  public:
    explicit PluginManageWindow(QWidget *)
    {
    }

    void exec()
    {
    }
};
