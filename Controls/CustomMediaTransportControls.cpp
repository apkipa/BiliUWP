#include "pch.h"
#include "CustomMediaTransportControls.h"
#if __has_include("CustomMediaTransportControls.g.cpp")
#include "CustomMediaTransportControls.g.cpp"
#endif
#include "util.hpp"

using namespace winrt;
using namespace Windows::System;
using namespace Windows::UI::ViewManagement;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::Media::Playback;

namespace winrt::BiliUWP::implementation {
    CustomMediaTransportControls::CustomMediaTransportControls() {}

    void CustomMediaTransportControls::OnPointerEntered(PointerRoutedEventArgs const& e) {
        this->Show();
    }
    void CustomMediaTransportControls::OnPointerExited(PointerRoutedEventArgs const& e) {
        VisualStateGroup vsg{ nullptr };
        auto root_grid = GetTemplateChild(L"RootGrid").as<Grid>();
        for (auto&& i : VisualStateManager::GetVisualStateGroups(root_grid)) {
            if (i.Name() == L"PlayPauseStates") {
                vsg = std::move(i);
                break;
            }
        }
        bool is_playing = vsg.CurrentState().Name() == L"PauseState";
        if (is_playing) {
            this->Hide();
        }
    }
    void CustomMediaTransportControls::OnKeyDown(KeyRoutedEventArgs const& e) {
        auto key = e.Key();
        if (key == VirtualKey::Escape) {
            auto app_view = ApplicationView::GetForCurrentView();
            if (app_view.IsFullScreenMode()) {
                app_view.ExitFullScreenMode();
            }
        }
        else if (key == VirtualKey::F11) {
            auto app_view = ApplicationView::GetForCurrentView();
            if (app_view.IsFullScreenMode()) {
                app_view.ExitFullScreenMode();
            }
            else {
                auto container = try_get_parent_container();
                if (!container) { return; }
                container.IsFullWindow(!container.IsFullWindow());
            }
        }
    }
    MediaPlayerElement CustomMediaTransportControls::try_get_parent_container() {
        auto dparent = VisualTreeHelper::GetParent(
            VisualTreeHelper::GetParent(VisualTreeHelper::GetParent(*this)));
        return dparent.try_as<MediaPlayerElement>();
    }
    MediaPlaybackCommandManager CustomMediaTransportControls::try_get_command_manager() {
        auto layout_root = VisualTreeHelper::GetParent(
            VisualTreeHelper::GetParent(*this)).try_as<Grid>();
        if (!layout_root) { return nullptr; }
        auto presenter = layout_root.FindName(L"MediaPlayerPresenter")
            .try_as<MediaPlayerPresenter>();
        if (!presenter) { return nullptr; }
        return presenter.MediaPlayer().CommandManager();
    }
}
