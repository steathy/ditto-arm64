# Strict decoding is shared by the source gate and release generator. PowerShell
# defaults differ between 5.1 and 7; neither is allowed to repair malformed text.
function Read-ReleaseText([string]$Path) {
    $decoder = New-Object Text.UTF8Encoding($false, $true)
    try { $text = $decoder.GetString([IO.File]::ReadAllBytes($Path)).TrimStart([char]0xfeff) }
    catch { throw "Text must be valid UTF-8: $Path" }
    if ($text.Contains([string][char]0xfffd)) { throw "Text contains a replacement character: $Path" }
    return $text
}
function ConvertTo-UpdateNotes([string]$Markdown) {
    $text = [regex]::Replace($Markdown, '(?m)^#{1,6}\s+', '')
    $text = [regex]::Replace($text, '\[([^\]]+)\]\((https://[^\s)]+)\)', '$1 ($2)')
    $text = $text.Replace('**','').Replace('`','')
    return $text.Trim()
}
