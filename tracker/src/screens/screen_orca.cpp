#include "screens.h"
#include "common.h"
#include "file_browser.h"
#include "corelib_gfx.h"
#include "corelib/corelib_file.h"
#include "chipnomad_lib.h"
extern "C" {
#include "orca/field.h"
}
#include <stdio.h>
#include <string.h>
#include <limits.h>

#ifndef WEB_BUILD
extern int fileReplace(const char* tempPath, const char* path);
#endif

// The editor owns its bounded UI copy. The audio thread only receives an
// immutable snapshot through chipnomadLoadOrcaScore().
static Glyph cells[ORCA_RUNTIME_MAX_CELLS];
static unsigned gridWidth = 32, gridHeight = 16;
static int cursorX, cursorY, scrollX, scrollY;
static int editing;
static Glyph packedCells[ORCA_RUNTIME_MAX_CELLS];
static char loadedScorePath[2048];
static int gridLoaded;
static const char entryChars[] = ".%:;+-*/=0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!?@#$&()[]{}<>^_~'\"\\|,`";
static int entryIndex;

static void fillEmpty(void) {
  memset(cells, '.', sizeof(cells));
  gridWidth = 32; gridHeight = 16; cursorX = cursorY = scrollX = scrollY = 0;
}

static void draw(void) {
  const ColorScheme cs = appSettings.colorScheme;
  gfxSetFgColor(cs.textTitles); gfxPrint(0, 0, "ORCA  EDIT:CHAR  EDIT+OPT:DEL  OPT:SAVE");
  PlaybackStatus status;
  memset(&status, 0, sizeof(status));
  if (chipnomadState) status = *chipnomadGetPlaybackStatus(chipnomadState);
  gfxSetFgColor(cs.textInfo);
  gfxPrintf(0, 1, "%s FRAME %06u  X%02d Y%02d", status.isPlaying ? "PLAY" : "STOP",
            status.orcaFrame, cursorX, cursorY);
  const int visibleW = 30, visibleH = 16;
  for (int y = 0; y < visibleH; ++y) {
    gfxSetFgColor(cs.textInfo); gfxPrintf(0, y + 3, "%02d", y + scrollY);
    for (int x = 0; x < visibleW; ++x) {
      int gx = x + scrollX, gy = y + scrollY;
      char ch = (gx < (int)gridWidth && gy < (int)gridHeight)
        ? (char)cells[gy * ORCA_RUNTIME_MAX_WIDTH + gx] : ' ';
      char value[2] = { ch, 0 };
      gfxSetFgColor(gx == cursorX && gy == cursorY ? cs.textValue : ch == '.' ? cs.textEmpty : cs.textDefault);
      gfxPrint(x + 3, y + 3, value);
    }
  }
  if (editing) {
    gfxSetFgColor(cs.textValue);
    gfxPrintf(0, 19, "CHAR: %c  EDIT=SET  ARROWS=CHOOSE", entryChars[entryIndex]);
  } else {
    gfxSetFgColor(cs.textInfo); gfxPrint(0, 19, "ARROWS MOVE  EDIT TYPE  EDIT+OPT CLEAR");
  }
  gfxCursor(cursorX - scrollX + 3, cursorY - scrollY + 3, 1);
}

static int loadScorePath(const char* path, int showMissing) {
  Field f; field_init(&f);
  Field_load_error result = field_load_file(path, &f);
  if (result != Field_load_error_ok || !f.width || !f.height || f.width > ORCA_RUNTIME_MAX_WIDTH || f.height > ORCA_RUNTIME_MAX_HEIGHT) {
    field_deinit(&f); fillEmpty();
    if (chipnomadState) chipnomadSetOrcaScoreEmpty(chipnomadState);
    if (showMissing) screenMessage(MESSAGE_TIME_ERROR, "%s", result == Field_load_error_cant_open_file ? "ORCA: Score not found" : "ORCA: Invalid score");
    snprintf(loadedScorePath, sizeof(loadedScorePath), "%s", path);
    gridLoaded = 1;
    return 0;
  }
  fillEmpty(); gridWidth = f.width; gridHeight = f.height;
  for (unsigned y = 0; y < gridHeight; ++y)
    memcpy(cells + y * ORCA_RUNTIME_MAX_WIDTH, f.buffer + y * f.width, f.width * sizeof(Glyph));
  field_deinit(&f);
  char error[128];
  if (!chipnomadLoadOrcaScore(chipnomadState, path, error, sizeof(error))) {
    fillEmpty(); screenMessage(MESSAGE_TIME_ERROR, "%s", error);
    snprintf(loadedScorePath, sizeof(loadedScorePath), "%s", path); gridLoaded = 1; return 0;
  }
  snprintf(loadedScorePath, sizeof(loadedScorePath), "%s", path); gridLoaded = 1;
  return 1;
}

static int publishGrid(void) {
  for (unsigned y = 0; y < gridHeight; ++y)
    memcpy(packedCells + y * gridWidth, cells + y * ORCA_RUNTIME_MAX_WIDTH,
           gridWidth * sizeof(Glyph));
  return chipnomadState && chipnomadPublishOrcaGrid(chipnomadState, packedCells,
                                                    (uint16_t)gridWidth, (uint16_t)gridHeight);
}

static void saveScore(const char* folder) {
  (void)folder;
  char path[2048];
  if (!fileBrowserGetSavePath(path, sizeof(path))) { screenMessage(MESSAGE_TIME_ERROR, "ORCA: Invalid save path"); return; }
  const size_t rootLength = strlen(appSettings.projectPath);
  if (rootLength && (strncmp(path, appSettings.projectPath, rootLength) != 0 ||
      (path[rootLength] != PATH_SEPARATOR_STR[0] && path[rootLength] != '\0'))) {
    screenMessage(MESSAGE_TIME_ERROR, "ORCA: Save inside project folder"); return;
  }
  const char* relative = path + (rootLength ? rootLength : 0);
  while (*relative == PATH_SEPARATOR_STR[0]) ++relative;
  if (strlen(relative) >= sizeof(chipnomadState->project.orcaScorePath)) {
    screenMessage(MESSAGE_TIME_ERROR, "ORCA: Score path is too long"); return;
  }
  for (unsigned y = 0; y < gridHeight; ++y)
    memcpy(packedCells + y * gridWidth, cells + y * ORCA_RUNTIME_MAX_WIDTH,
           gridWidth * sizeof(Glyph));
  Field f; field_init(&f);
  f.width = (U16)gridWidth; f.height = (U16)gridHeight;
  f.buffer = packedCells; // field_fput does not take ownership
  char tempPath[2080];
#ifdef WEB_BUILD
  snprintf(tempPath, sizeof(tempPath), "%s", path);
#else
  snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
#endif
  FILE* file = fopen(tempPath, "wb");
  if (!file) { screenMessage(MESSAGE_TIME_ERROR, "ORCA: Could not save score"); return; }
  field_fput(&f, file);
  int failed = ferror(file);
  if (fclose(file) != 0) failed = 1;
  if (failed) { remove(tempPath); screenMessage(MESSAGE_TIME_ERROR, "ORCA: Could not save score"); return; }
#ifndef WEB_BUILD
  if (fileReplace(tempPath, path) != 0) {
    remove(tempPath);
    screenMessage(MESSAGE_TIME_ERROR, "ORCA: Could not replace score"); return;
  }
#endif
  if (chipnomadState) {
    snprintf(chipnomadState->project.orcaScorePath, sizeof(chipnomadState->project.orcaScorePath), "%s", relative);
    projectModified = 1;
    char error[128];
    if (!chipnomadLoadOrcaScore(chipnomadState, path, error, sizeof(error)))
      screenMessage(MESSAGE_TIME_ERROR, "%s", error);
    else screenMessage(MESSAGE_TIME, "ORCA score saved");
  }
}

static void cancelSave(void) {}
static void openSave(void) {
  char folder[2048], filename[128];
  snprintf(folder, sizeof(folder), "%s", appSettings.projectPath);
  const char* relative = chipnomadState ? chipnomadState->project.orcaScorePath : "";
  const char* basename = relative;
  const char* slash = strrchr(relative, '/');
  const char* backslash = strrchr(relative, '\\');
  const char* separator = !slash ? backslash : !backslash ? slash : slash > backslash ? slash : backslash;
  if (separator) {
    size_t dirLength = (size_t)(separator - relative);
    if (dirLength && strlen(folder) + dirLength + 2 < sizeof(folder)) {
      size_t oldLength = strlen(folder);
      if (oldLength) strncat(folder, PATH_SEPARATOR_STR, sizeof(folder) - oldLength - 1);
      strncat(folder, relative, dirLength);
    }
    basename = separator + 1;
  }
  snprintf(filename, sizeof(filename), "%s", basename && *basename ? basename : appSettings.projectFilename);
  size_t filenameLength = strlen(filename);
  if (filenameLength >= 5 && strcmp(filename + filenameLength - 5, ".orca") == 0)
    filename[filenameLength - 5] = 0;
  fileBrowserSetupFolderMode("SAVE ORCA SCORE", folder,
    filename, ".orca", saveScore, cancelSave);
  screenSetup(&screenFileBrowser, 0);
}

static void setup(int) {
  editing = 0;
  if (!chipnomadState) { fillEmpty(); return; }
  if (!chipnomadState->project.orcaScorePath[0]) {
    snprintf(chipnomadState->project.orcaScorePath, sizeof(chipnomadState->project.orcaScorePath), "%s.orca", appSettings.projectFilename[0] ? appSettings.projectFilename : "orca");
  }
  char path[2048];
  if (appSettings.projectPath[0]) snprintf(path, sizeof(path), "%s%s%s", appSettings.projectPath, PATH_SEPARATOR_STR, chipnomadState->project.orcaScorePath);
  else snprintf(path, sizeof(path), "%s", chipnomadState->project.orcaScorePath);
  if (!gridLoaded || strcmp(path, loadedScorePath) != 0) loadScorePath(path, 1);
}

static int input(int down, int keys, int taps) {
  if (!down && keys) return 1;
  if (editing) {
    if (keys == keyOpt) { editing = 0; draw(); return 1; }
    if (keys == keyEdit) {
      cells[cursorY * ORCA_RUNTIME_MAX_WIDTH + cursorX] = (Glyph)entryChars[entryIndex];
      if ((unsigned)cursorX >= gridWidth) gridWidth = cursorX + 1;
      if ((unsigned)cursorY >= gridHeight) gridHeight = cursorY + 1;
      editing = 0; projectModified = 1;
      // Audio publication is a bounded copy; file parsing stays on the UI thread.
      if (!publishGrid())
        screenMessage(MESSAGE_TIME_ERROR, "ORCA: Audio update failed");
      draw(); return 1;
    }
    const char* pos = strchr(entryChars, entryChars[entryIndex]); (void)pos;
    if (keys == keyLeft || keys == keyUp) entryIndex = (entryIndex + (int)sizeof(entryChars) - 2) % ((int)sizeof(entryChars) - 1);
    else if (keys == keyRight || keys == keyDown) entryIndex = (entryIndex + 1) % ((int)sizeof(entryChars) - 1);
    draw(); return 1;
  }
  if (keys == keyOpt) { openSave(); return 1; }
  if (keys == keyEdit) { editing = 1; entryIndex = 0; draw(); return 1; }
  if (keys == (keyEdit | keyOpt)) {
    cells[cursorY * ORCA_RUNTIME_MAX_WIDTH + cursorX] = '.'; projectModified = 1;
    publishGrid();
    draw(); return 1;
  }
  if (keys == (keyShift | keyLeft)) { screenSetup(&screenMixer, 0); return 1; }
  if (keys == (keyShift | keyRight)) { screenSetup(&screenInstrument, 0); return 1; }
  if (keys == (keyShift | keyUp)) { screenSetup(&screenProject, 0); return 1; }
  if (keys == (keyShift | keyDown)) { screenSetup(&screenViewSettings, 0); return 1; }
  if (keys == keyLeft && cursorX > 0) --cursorX;
  else if (keys == keyRight && cursorX + 1 < (int)ORCA_RUNTIME_MAX_WIDTH) ++cursorX;
  else if (keys == keyUp && cursorY > 0) --cursorY;
  else if (keys == keyDown && cursorY + 1 < (int)ORCA_RUNTIME_MAX_HEIGHT) ++cursorY;
  else return 0;
  if (cursorX < scrollX) scrollX = cursorX;
  if (cursorX >= scrollX + 30) scrollX = cursorX - 29;
  if (cursorY < scrollY) scrollY = cursorY;
  if (cursorY >= scrollY + 16) scrollY = cursorY - 15;
  draw(); (void)taps; return 1;
}

static ScreenPlaybackLevel playback(void) { return ScreenPlaybackLevel::song; }
const AppScreen screenOrca = {NULL, setup, draw, draw, input, playback};
void screenOrcaProjectLoaded(void) {
  if (!chipnomadState) return;
  if (chipnomadState->project.viewMode == ViewMode::orca) { gridLoaded = 0; setup(0); }
  else { gridLoaded = 0; chipnomadSetOrcaScoreEmpty(chipnomadState); }
}
void screenOrcaModeChanged(void) {
  if (chipnomadState) {
    chipnomadQueuePlaybackStop(chipnomadState);
    chipnomadQueueProjectRefresh(chipnomadState);
  }
  if (chipnomadState && chipnomadState->project.viewMode == ViewMode::orca) screenSetup(&screenOrca, 0);
  else {
    if (chipnomadState) chipnomadSetOrcaScoreEmpty(chipnomadState);
    screenSetup(&screenSong, 0);
  }
}
char screenOrcaGetCell(int x, int y) {
  if (x < 0 || y < 0 || x >= (int)ORCA_RUNTIME_MAX_WIDTH || y >= (int)ORCA_RUNTIME_MAX_HEIGHT) return 0;
  return (char)cells[y * ORCA_RUNTIME_MAX_WIDTH + x];
}
void screenOrcaGetCursor(int* x, int* y) { if (x) *x = cursorX; if (y) *y = cursorY; }
