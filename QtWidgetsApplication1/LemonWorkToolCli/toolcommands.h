#pragma once

#include <QJsonArray>
class CliArguments;
class CliResult;

// Returns the command descriptions used by CLI help and the API manual.
QJsonArray toolCommandHelp();
// Reads the running workbench's current Cookie through the local Auth pipe.
CliResult runDesktopCookieCommand(const CliArguments &args);
// Dispatches the workbench and file conversion commands.
CliResult runToolCommand(const CliArguments &args);
