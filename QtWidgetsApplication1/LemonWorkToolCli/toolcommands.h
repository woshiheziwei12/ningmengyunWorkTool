#pragma once

#include <QJsonArray>
class CliArguments;
class CliResult;

// Returns the command descriptions used by CLI help and the API manual.
QJsonArray toolCommandHelp();
// Dispatches the document and file conversion commands.
CliResult runToolCommand(const CliArguments &args);
