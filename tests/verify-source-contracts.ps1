$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

function Assert-Contains([string]$Path, [string]$Expected, [string]$Description) {
  $content = Get-Content -Raw -Path (Join-Path $root $Path)
  if (-not $content.Contains($Expected)) {
    throw "Regression check failed: $Description"
  }
}

# Empty folders have no child-file completion event, so the sender must emit
# an explicit folder-complete message and both sides must process it.
Assert-Contains 'lmc/src/filemessagingproc.cpp' `
  'if(folderList[index].fileList.isEmpty()) {' `
  'empty folders must send an explicit FO_Complete message'
Assert-Contains 'lmc/src/filemessagingproc.cpp' `
  'pMessage->addData(XN_FILEOP, FileOpNames[FO_Complete]);' `
  'empty-folder completion must be encoded in the outgoing message'
Assert-Contains 'lmc/src/filemessagingproc.cpp' `
  'case FO_Complete:' `
  'received folder completion must update the transfer state'
Assert-Contains 'lmc/src/filemessagingproc.cpp' `
  'if(folderList[index].fileCount == 0) {' `
  'an accepted empty folder must complete immediately on the receiver'
Assert-Contains 'lmc/src/filemessagingproc.cpp' `
  'emit messageReceived(MT_Folder, lpszUserId, &completeMessage);' `
  'the receiver must publish the empty-folder completion to its UI'

# Saved history must contain a real horizontal space between time and text.
Assert-Contains 'lmc/src/messagelog.cpp' `
  '"</span> "\' `
  'saved message history must separate timestamp and message with a space'
Assert-Contains 'lmc/src/historywindow.cpp' `
  'data.replace("</span><span class=''message''>", "</span> <span class=''message''>");' `
  'legacy history display must repair missing timestamp spacing'

# Classic transfer requests use a fixed icon column plus padding, avoiding
# the old icon/text overlap at common Windows scale factors.
Assert-Contains 'lmc/src/resources/themes/Classic/Request.html' `
  "width='24' valign='top' style='padding-top: 2px; padding-right: 8px;'" `
  'Classic transfer request must reserve a padded icon column'
Assert-Contains 'lmc/src/resources/themes/Classic/Request.html' `
  "width='16' height='16'" `
  'Classic transfer request must reserve space for the 16px icon'

# A completed transfer is terminal. Peer-offline cleanup consults the
# pending-operation map, so it must be synchronized with the visible log.
Assert-Contains 'lmc/src/messagelog.cpp' `
  'if(currentOp == FO_Complete && op != FO_Complete)' `
  'completed chat transfers must not regress after a disconnect'
Assert-Contains 'lmc/src/messagelog.cpp' `
  '(mode == FM_Send) ? sendFileMap : receiveFileMap;' `
  'file status updates must synchronize the pending-operation map'
Assert-Contains 'lmc/src/transferwindow.cpp' `
  'if(view->state == FileView::TS_Complete)' `
  'completed transfer-list entries must ignore late terminal errors'

# Classic exists both in the embedded resource and in the installed themes
# folder. Theme discovery must show each name only once.
Assert-Contains 'lmc/src/theme.cpp' `
  'if(themeNames.contains(dirName))' `
  'theme discovery must suppress duplicate theme names'

Write-Host 'Source regression contracts passed.'
