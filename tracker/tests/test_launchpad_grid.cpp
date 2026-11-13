#include "launchpad_grid.h"
#include "doctest.h"

TEST_CASE("Launchpad Programmer Mode coordinates map to tracks and visible song rows") {
  int column = -1, visibleRow = -1;
  CHECK(launchpadGrid::noteFor(0, 0) == 81); // upper-left
  CHECK(launchpadGrid::noteFor(7, 0) == 88); // upper-right
  CHECK(launchpadGrid::noteFor(0, 7) == 11); // lower-left
  CHECK(launchpadGrid::noteFor(7, 7) == 18); // lower-right
  CHECK(launchpadGrid::coordinatesForNote(81, &column, &visibleRow));
  CHECK(column == 0);
  CHECK(visibleRow == 0);
  CHECK(launchpadGrid::coordinatesForNote(18, &column, &visibleRow));
  CHECK(column == 7);
  CHECK(visibleRow == 7);
  CHECK_FALSE(launchpadGrid::coordinatesForNote(19, &column, &visibleRow)); // side button
  CHECK(launchpadGrid::songRowForVisibleRow(24, 0) == 24);
  CHECK(launchpadGrid::songRowForVisibleRow(24, 7) == 31);
  CHECK(launchpadGrid::songRowForVisibleRow(32, 0) == 32); // viewport follows topRow
  int sceneRow = -1;
  CHECK(launchpadGrid::songRowForSceneControl(89, 24, &sceneRow));
  CHECK(sceneRow == 24);
  CHECK(launchpadGrid::songRowForSceneControl(19, 24, &sceneRow));
  CHECK(sceneRow == 31);
  CHECK(launchpadGrid::sceneControlForVisibleRow(0) == 89);
  CHECK(launchpadGrid::sceneControlForVisibleRow(7) == 19);
  CHECK_FALSE(launchpadGrid::songRowForSceneControl(20, 24, &sceneRow));
}

TEST_CASE("Launchpad LED states prioritize playing over queued over populated") {
  CHECK(launchpadGrid::colour(false, false, false) == launchpadGrid::kOff);
  CHECK(launchpadGrid::colour(true, false, false) == launchpadGrid::kBlue);
  CHECK(launchpadGrid::colour(true, true, false) == launchpadGrid::kAmber);
  CHECK(launchpadGrid::colour(true, true, true) == launchpadGrid::kGreen);
  CHECK(launchpadGrid::colour(false, false, true) == launchpadGrid::kGreen);
}

TEST_CASE("Launchpad Song layout maps the four Programmer Mode navigation keys") {
  using launchpadGrid::SongNavigation;
  CHECK(launchpadGrid::songNavigationForEvent(0xb0, 91) == SongNavigation::up);
  CHECK(launchpadGrid::songNavigationForEvent(0xb0, 92) == SongNavigation::down);
  CHECK(launchpadGrid::songNavigationForEvent(0xb0, 93) == SongNavigation::left);
  CHECK(launchpadGrid::songNavigationForEvent(0xb0, 94) == SongNavigation::right);
  CHECK(launchpadGrid::songNavigationForEvent(0xb0, 95) == SongNavigation::none);
  CHECK(launchpadGrid::songNavigationForEvent(0x90, 92) == SongNavigation::none);
}

TEST_CASE("Launchpad double presses track the same released control and reject held duplicates") {
  launchpadGrid::PressTracker presses;
  CHECK(presses.press(3, 20) == 1);
  CHECK(presses.press(3, 20) == 0); // duplicate Note On while held
  presses.release(3);
  CHECK(presses.press(3, 20) == 2);
  presses.release(3);
  CHECK(presses.press(4, 20) == 1); // different pad is a new sequence
  presses.release(4);
  presses.tick();
  CHECK(presses.press(4, 20) == 2);
  presses.release(4);
  for (int i = 0; i < 20; ++i) presses.tick();
  CHECK(presses.press(4, 20) == 1); // expired interval
  presses.reset();
  CHECK(presses.press(64, 20) == 1); // scene controls have separate identities
}

TEST_CASE("Launchpad LED state reflects playing, urgent queue, queued stop and screen scope") {
  using launchpadGrid::LedState;
  CHECK(launchpadGrid::ledState(false, false, false, false) == LedState::off);
  CHECK(launchpadGrid::ledState(true, false, false, false) == LedState::populated);
  CHECK(launchpadGrid::ledState(true, true, false, false) == LedState::queued);
  CHECK(launchpadGrid::ledState(true, true, true, false) == LedState::urgent);
  CHECK(launchpadGrid::ledState(false, true, true, false) == LedState::urgent); // queued stop
  CHECK(launchpadGrid::ledState(true, true, true, true) == LedState::playing);
  CHECK(launchpadGrid::colourForState(LedState::urgent) == launchpadGrid::kUrgentAmber);
  CHECK(launchpadGrid::isScreenActive(true, true));
  CHECK_FALSE(launchpadGrid::isScreenActive(false, true));
  CHECK_FALSE(launchpadGrid::isScreenActive(true, false));
}

TEST_CASE("Launchpad Mixer performance layout reserves lower pads without changing Song mode") {
  using launchpadGrid::Layout;
  int column = -1, row = -1;
  CHECK(launchpadGrid::layoutFor(false, true, true, 0) == Layout::inactive);
  CHECK(launchpadGrid::layoutFor(true, true, true, 0) == Layout::song);
  CHECK(launchpadGrid::layoutFor(true, false, true, 0) == Layout::mixerPerformance);
  CHECK(launchpadGrid::layoutFor(true, false, true, 1) == Layout::inactive);
  CHECK(launchpadGrid::mixerSceneRow(0));
  CHECK(launchpadGrid::mixerSceneRow(3));
  CHECK_FALSE(launchpadGrid::mixerSceneRow(4));
  CHECK_FALSE(launchpadGrid::mixerSceneRow(7));
  CHECK(launchpadGrid::mixerPerformancePad(11));
  CHECK(launchpadGrid::mixerPerformancePad(12));
  CHECK(launchpadGrid::mixerPerformancePad(41));
  CHECK_FALSE(launchpadGrid::mixerPerformancePad(51));
  CHECK(launchpadGrid::performanceGateForVisibleRow(7) == 0);
  CHECK(launchpadGrid::performanceGateForVisibleRow(6) == 1);
  CHECK(launchpadGrid::performanceGateForVisibleRow(5) == 2);
  CHECK(launchpadGrid::performanceGateForVisibleRow(4) == 3);
  CHECK(launchpadGrid::performanceGateForVisibleRow(3) == -1);
  CHECK(launchpadGrid::performancePadBit(0, 0) == 0);
  CHECK(launchpadGrid::performancePadBit(7, 3) == 31);
  CHECK(launchpadGrid::performancePadBit(8, 0) == -1);
  CHECK(launchpadGrid::performancePadColour(false, false) == launchpadGrid::kOff);
  CHECK(launchpadGrid::performancePadColour(true, false) == launchpadGrid::kWhite);
  CHECK(launchpadGrid::performancePadColour(true, true) == launchpadGrid::kGreen);
  const uint32_t held = UINT32_C(1) << launchpadGrid::performancePadBit(7, 3);
  CHECK(launchpadGrid::performancePadHeld(held, 7, 3));
  CHECK_FALSE(launchpadGrid::performancePadHeld(held, 6, 3));
  CHECK(launchpadGrid::performanceGateNeedsRelease(true, Layout::mixerPerformance, Layout::song, true));
  CHECK(launchpadGrid::performanceGateNeedsRelease(true, Layout::mixerPerformance, Layout::inactive, false));
  CHECK_FALSE(launchpadGrid::performanceGateNeedsRelease(false, Layout::mixerPerformance, Layout::inactive, false));
  CHECK_FALSE(launchpadGrid::performanceGateNeedsRelease(true, Layout::mixerPerformance, Layout::mixerPerformance, true));
  CHECK(launchpadGrid::coordinatesForNote(11, &column, &row));
  CHECK(column == 0);
  CHECK(row == 7);
  CHECK(launchpadGrid::songRowForVisibleRow(24, 7) == 31); // unchanged Song mapping
}
