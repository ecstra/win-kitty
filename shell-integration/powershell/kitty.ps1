# kitty shell integration for PowerShell.
#
# kitty dot-sources this at startup (see setup_powershell_env in
# kitty/shell_integration.py). It wraps the prompt so kitty gets the escape codes
# it needs: OSC 133;A marks the start of each prompt so prompt jumping works, and
# OSC 2 sets the window title to the working directory so tabs show the folder
# rather than the pwsh path conhost reports. The current directory for new tabs
# is read separately from the process, so no OSC 7 is emitted here.

# Guard against wrapping the prompt more than once in a session.
if ($global:__kitty_integration_installed) { return }
$global:__kitty_integration_installed = $true

$global:__kitty_opts = "$env:KITTY_SHELL_INTEGRATION"
$global:__kitty_orig_prompt = $function:prompt

function global:prompt {
    $ec = [char]27
    $st = "$ec\"
    $out = ''

    if ($global:__kitty_opts -notmatch 'no-prompt-mark') {
        $out += "$ec]133;A$st"
    }

    if ($global:__kitty_opts -notmatch 'no-title') {
        $cwd = (Get-Location).Path
        $out += "$ec]2;$cwd$([char]7)"
    }

    if ($global:__kitty_orig_prompt) {
        $out += & $global:__kitty_orig_prompt
    } else {
        $out += "PS $((Get-Location).Path)> "
    }

    $out
}
