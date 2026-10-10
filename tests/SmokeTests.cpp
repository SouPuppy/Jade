#include <Jade/Jade.h>

#include <gtest/gtest.h>

TEST(Application, ThreeClearFrames)
{
  Jade::ApplicationConfig config;
  config.window.visible = false;
  config.vsync = false;
  Jade::Application app(config);
  EXPECT_NO_THROW(app.Run({ 3, true }));
}
