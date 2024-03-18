#include <imgui.h>
#include <implot.h>

#include <uikit/main.hpp>
#include <uikit/viewport.hpp>

#include <array>
#include <iomanip>
#include <iterator>
#include <optional>
#include <random>
#include <set>
#include <sstream>

#include <cstdint>

#include <spdlog/spdlog.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>

namespace {

struct selection final
{
  int offset{};

  /**
   * @brief The ending offset (inclusive).
   * */
  int end_offset{};
};

class app_impl final : public uikit::app
{
public:
  void setup(uikit::platform& plt) override
  {
    plt.set_app_name("Video Tool");

    m_video_capture.open("%05d.png");

    m_num_frames = m_video_capture.get(cv::CAP_PROP_FRAME_COUNT);

    glGenTextures(1, &m_current_frame);

    glBindTexture(GL_TEXTURE_2D, m_current_frame);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    load_current_texture();
  }

  void teardown(uikit::platform&) override { glDeleteTextures(1, &m_current_frame); }

  void loop(uikit::platform& plt) override
  {
    process_export();

    if (ImGui::IsKeyPressed(ImGuiKey_LeftShift, false)) {
      m_current_selection = selection{ m_current_frame_index, 1 };
    }

    if (ImGui::IsKeyReleased(ImGuiKey_LeftShift)) {
      insert_selection(m_current_selection.value());
      m_current_selection.reset();
    }

    if (m_current_selection.has_value()) {
      m_current_selection->end_offset = m_current_frame_index;
    }

    const auto size = ImGui::GetIO().DisplaySize;

    ImVec2 menu_size{ 0, 0 };

    if (ImGui::BeginMainMenuBar()) {

      if (ImGui::BeginMenu("Export")) {

        render_export_menu();

        ImGui::EndMenu();
      }

      menu_size = ImGui::GetWindowSize();

      ImGui::EndMainMenuBar();
    }

    ImGui::SetNextWindowPos(ImVec2(0, menu_size.y), ImGuiCond_Always);

    ImGui::SetNextWindowSize(ImVec2(size.x, size.y - menu_size.y), ImGuiCond_Always);

    if (ImGui::Begin("Window", nullptr, ImGuiWindowFlags_NoDecoration)) {

      if (m_in_export_state) {

        poll_export();

        const float alpha = static_cast<float>(m_current_export_frame) / static_cast<float>(m_frame_indices.size());

        ImGui::ProgressBar(alpha);
      }

      render_slider();

      render_plot_window();
    }
    ImGui::End();
  }

protected:
  void process_export()
  {
    if (m_export_queued) {

      start_export();

      m_export_queued = false;
    }
  }

  void render_export_menu()
  {
    ImGui::BeginDisabled(m_in_export_state);

    ImGui::InputInt("Export Width", &m_export_width);

    ImGui::InputInt("Export Height", &m_export_height);

    if (ImGui::Button("Export")) {
      m_export_queued = true;
    }

    ImGui::EndDisabled();
  }

  void render_slider()
  {
    const int max = (m_num_frames > 0) ? (m_num_frames - 1) : 0;

    ImGui::BeginDisabled(m_num_frames == 0);

    if (ImGui::DragInt("Frame", &m_current_frame_index, 1, 0, max)) {
      load_current_texture();
    }

    ImGui::EndDisabled();

    ImGui::Text("Selection Size: %d of %d", static_cast<int>(m_frame_indices.size()), m_num_frames);

    if (m_current_selection) {
      const auto s = m_current_selection.value();
      ImGui::Text("Selection: [%d, %d]", s.offset, s.end_offset);
    }
  }

  void render_plot_window()
  {
    if (!ImPlot::BeginPlot("##FrameViewer", ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_Crosshairs)) {
      return;
    }

    ImPlot::SetupAxes("X", "Y", ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoDecorations);

    ImPlot::PlotImage(
      "##CurrentFrame", reinterpret_cast<ImTextureID>(m_current_frame), ImPlotPoint(0, 0), ImPlotPoint(1, 1));

    ImPlot::EndPlot();
  }

  void load_current_texture()
  {
    if (m_current_frame_index >= m_num_frames) {
      make_current_texture_null();
      return;
    }

    if (!m_video_capture.set(cv::CAP_PROP_POS_FRAMES, m_current_frame_index)) {
      spdlog::error("Failed to seek to frame {}.", m_current_frame_index);
    }

    cv::Mat frame;

    if (!m_video_capture.read(frame)) {
      spdlog::error("Failed to read frame {}.", m_current_frame_index);
    }

    const auto frame_size = frame.size();
    const auto frame_w = frame_size.width;
    const auto frame_h = frame_size.height;

    std::vector<std::uint8_t> rgb(frame_size.area() * 3, 0);

    for (int i = 0; i < frame_size.area(); i++) {

      const auto pixel = frame.at<cv::Vec3b>(i);

      rgb[i * 3 + 0] = pixel[2];
      rgb[i * 3 + 1] = pixel[1];
      rgb[i * 3 + 2] = pixel[0];
    }

    glBindTexture(GL_TEXTURE_2D, m_current_frame);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame_w, frame_h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void make_current_texture_null()
  {
    std::array<std::uint8_t, 3 * 4> color{};

    glBindTexture(GL_TEXTURE_2D, m_current_frame);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, color.data());
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void insert_selection(const selection& s)
  {
    for (int i = s.offset; i <= s.end_offset; i++) {
      m_frame_indices.emplace(i);
    }
  }

  void start_export()
  {
    m_in_export_state = true;

    m_current_export_frame = 0;

    /* Also export randomly selected null samples.
     * */

    std::vector<int> null_indices;

    for (int i = 0; i < m_num_frames; i++) {

      if (m_frame_indices.find(i) != m_frame_indices.end()) {
        continue;
      }

      null_indices.emplace_back(i);
    }

    std::mt19937 rng(0);

    for (int i = 0; i < 4; i++) {
      shuffle_indices(null_indices, rng);
    }

    const auto min_size = std::min(m_frame_indices.size(), static_cast<std::size_t>(m_num_frames));

    null_indices.resize(min_size);

    m_unselected_indices = std::move(null_indices);
  }

  auto poll_export() -> bool
  {
    if (m_current_export_frame >= static_cast<int>(m_frame_indices.size())) {
      m_in_export_state = false;
      return false;
    }

    using clock_type = std::chrono::high_resolution_clock;

    using time_point = typename clock_type::time_point;

    const auto t0 = clock_type::now();

    while ((m_current_export_frame < static_cast<int>(m_frame_indices.size()))) {

      { // export selected sample

        std::ostringstream path_stream;
        path_stream << "1_" << std::setfill('0') << std::setw(8) << m_current_export_frame << ".png";
        const auto path = path_stream.str();

        const auto index = *std::next(m_frame_indices.begin(), m_current_export_frame);

        if (!m_video_capture.set(cv::CAP_PROP_POS_FRAMES, index)) {
          spdlog::error("Failed to seek to frame {}.", index);
        }

        cv::Mat frame;

        if (!m_video_capture.read(frame)) {
          spdlog::error("Failed to read frame {}.", index);
        }

        cv::Mat resized_frame;

        cv::resize(frame, resized_frame, cv::Size(m_export_width, m_export_height));

        cv::imwrite(path, resized_frame);
      }

      if (m_export_unselected) {

        std::ostringstream path_stream;
        path_stream << "0_" << std::setfill('0') << std::setw(8) << m_current_export_frame << ".png";
        const auto path = path_stream.str();

        const auto index = m_unselected_indices.at(m_current_export_frame);

        m_video_capture.set(cv::CAP_PROP_POS_FRAMES, index);

        cv::Mat frame;

        if (!m_video_capture.set(cv::CAP_PROP_POS_FRAMES, index)) {
          spdlog::error("Failed to seek to frame {}.", index);
        }

        if (!m_video_capture.read(frame)) {
          spdlog::error("Failed to read frame {}.", index);
        }

        cv::Mat resized_frame;

        cv::resize(frame, resized_frame, cv::Size(m_export_width, m_export_height));

        cv::imwrite(path, resized_frame);
      }

      m_current_export_frame++;

      const auto t1 = clock_type::now();

      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);

      if (elapsed > std::chrono::milliseconds(50)) {
        break;
      }
    }

    return true;
  }

  template<typename Rng>
  static void shuffle_indices(std::vector<int>& in, Rng& rng)
  {
    for (int i = 2; i < static_cast<int>(in.size()); i++) {

      std::uniform_int_distribution<int> dist(0, i - 1);

      const auto index = dist(rng);

      std::swap(in.at(i), in.at(index));
    }
  }

private:
  cv::VideoCapture m_video_capture;

  bool m_crop{ false };

  float m_crop_offset[2]{ 0, 0 };

  float m_crop_size[2]{ 1, 1 };

  int m_export_width{ 224 };

  int m_export_height{ 224 };

  bool m_export_unselected{ true };

  int m_num_frames{};

  int m_current_frame_index{};

  GLuint m_current_frame{};

  std::set<int> m_frame_indices;

  std::vector<int> m_unselected_indices;

  std::optional<selection> m_current_selection;

  bool m_export_queued{ false };

  bool m_in_export_state{ false };

  int m_current_export_frame{};
};

} // namespace

namespace uikit {

auto
app::create() -> std::unique_ptr<app>
{
  return std::unique_ptr<app>(new app_impl());
}

} // namespace uikit
