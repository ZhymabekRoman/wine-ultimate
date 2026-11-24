/* WinRT Windows.UI.Xaml shared interface GUID declarations
 *
 * Copyright (C) 2026 Wine project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_WINDOWS_UI_XAML_UWP_GUIDS_H
#define __WINE_WINDOWS_UI_XAML_UWP_GUIDS_H

#include "windef.h"
#include "guiddef.h"

/* WIC / WeakRef / ReferenceTracker / MetadataProvider */
extern const GUID CLSID_WICImagingFactory_local;
extern const GUID GUID_WICPixelFormat32bppBGRA_local;
extern const GUID IID_IWICImagingFactory_local;
extern const GUID IID_IWeakReference_local;
extern const GUID IID_IWeakReferenceSource_local;
extern const GUID IID_IReferenceTracker;
extern const GUID IID_IReferenceTrackerTarget;
extern const GUID IID_IReferenceTrackerTarget_UWP;
extern const GUID IID_IReferenceTrackerTarget_UWP2;
extern const GUID IID_IReferenceTrackerTarget_WinUI;
extern const GUID IID_IReferenceTracker_UWP;
extern const GUID IID_IXamlMetadataProvider_WinUI;
extern const GUID IID_IXamlMetadataProvider_UWP;
extern const GUID IID_IXamlType_UWP_g;
extern const GUID IID_IXamlType_WinUI3_g;
extern const GUID IID_IComponentConnector_UWP_g;
extern const GUID IID_IComponentConnector2_UWP_g;
extern const GUID IID_IComponentConnector_WinUI;

/* Core / Property / Collection / Stringable IIDs */
extern const GUID IID_IPropertyValue_g;
extern const GUID IID_IUriRuntimeClass_g;
extern const GUID IID_IStringable_UWP;
extern const GUID IID_IBindableIterable_UWP;
extern const GUID IID_IBindableVector_UWP;
extern const GUID IID_IIterable_IInspectable_UWP;
extern const GUID IID_IVector_IInspectable_UWP;
extern const GUID IID_IVectorView_IInspectable_WinUI;
extern const GUID IID_IVectorView_Object_UWP;
extern const GUID IID_INotifyPropertyChanged_UWP;
extern const GUID IID_IPropertyChangedEventArgs_UWP;
extern const GUID IID_IPropertyChangedEventHandler_UWP;
extern const GUID IID_IPropertyPath_UWP;
extern const GUID IID_IPropertyPathFactory_UWP;
extern const GUID IID_IDependencyPropertyChangedCallback_UWP;
extern const GUID IID_IDependencyPropertyChangedEventArgs_UWP;
extern const GUID IID_IDataContextChangedEventArgs_UWP;
extern const GUID IID_IReference_NumbersAndOperatorsEnum;

/* UWP / WinUI XAML element & control IIDs */
extern const GUID IID_IAutoSuggestBox_Real_UWP;
extern const GUID IID_IAutoSuggestBox2_Real_UWP;
extern const GUID IID_IBindingFactory_UWP;
extern const GUID IID_IBorder2_Calc;
extern const GUID IID_IBorder_UWP;
extern const GUID IID_IBrush_UWP;
extern const GUID IID_IButtonBaseFactory_UWP;
extern const GUID IID_IButtonBaseStatics_UWP;
extern const GUID IID_IButtonBase_UWP;
extern const GUID IID_IButtonFactory_UWP;
extern const GUID IID_IButtonWithFlyout_Real_UWP;
extern const GUID IID_IButtonWithFlyout_UWP;
extern const GUID IID_IButton_UWP;
extern const GUID IID_ICanvasStatics_UWP;
extern const GUID IID_IColumnDefinition_UWP;
extern const GUID IID_ICommand_UWP;
extern const GUID IID_ICommand_UWP2;
extern const GUID IID_IContentControl2_UWP;
extern const GUID IID_IContentControlFactory_UWP;
extern const GUID IID_IContentControlFactory_UWP2;
extern const GUID IID_IContentControlOverrides_UWP;
extern const GUID IID_IContentControlOvr_Calc;
extern const GUID IID_IContentControl_UWP;
extern const GUID IID_IContentDialog2_UWP;
extern const GUID IID_IContentDialogClosedEventArgs_UWP;
extern const GUID IID_IContentDialogFactory_UWP;
extern const GUID IID_IContentDialog_UWP;
extern const GUID IID_IContentDialog_UWP_Alt;
extern const GUID IID_IContentPresenterFactory_UWP;
extern const GUID IID_IContentPresenter_UWP;
extern const GUID IID_IControl2_UWP;
extern const GUID IID_IControl3_UWP;
extern const GUID IID_IControl4_UWP;
extern const GUID IID_IControl5_UWP;
extern const GUID IID_IControlFactory_UWP;
extern const GUID IID_IControlOverrides_UWP;
extern const GUID IID_IControlProtected_UWP;
extern const GUID IID_IControl_UWP;
extern const GUID IID_IDataTemplateSelector2_UWP;
extern const GUID IID_IDataTemplateSelectorFactory_UWP;
extern const GUID IID_IDataTemplateSelectorOverrides2_UWP;
extern const GUID IID_IDataTemplateSelectorOverrides_UWP;
extern const GUID IID_IDataTemplateSelector_UWP;
extern const GUID IID_IDataTemplate_UWP;
extern const GUID IID_IDependencyObject2_UWP;
extern const GUID IID_IDependencyObjectCollectionFactory_UWP;
extern const GUID IID_IDependencyObjectFactory_UWP;
extern const GUID IID_IDependencyObject_UWP;
extern const GUID IID_IDependencyObject_WinUI;
extern const GUID IID_IDesktopWindowXamlSourceFactory_UWP;
extern const GUID IID_IDesktopWindowXamlSourceNative2_UWP;
extern const GUID IID_IDesktopWindowXamlSourceNative_UWP;
extern const GUID IID_IDesktopWindowXamlSource_UWP;
extern const GUID IID_IDispatcherTimerFactory_UWP;
extern const GUID IID_IDispatcherTimer_UWP;
extern const GUID IID_IEasingFunctionBase_UWP;
extern const GUID IID_IEffectiveViewportChangedEventArgs_UWP;
extern const GUID IID_IEntranceNavigationTransitionInfo_UWP;
extern const GUID IID_IExecuteRequestedEventArgs_UWP;
extern const GUID IID_IFlyoutBase3_UWP;
extern const GUID IID_IFlyoutBase5_UWP;
extern const GUID IID_IFlyoutBaseFactory_UWP;
extern const GUID IID_IFlyoutBase_UWP;
extern const GUID IID_IFontFamilyFactory_UWP;
extern const GUID IID_IFontFamily_UWP;
extern const GUID IID_IFontIcon_UWP;
extern const GUID IID_IFrame3_UWP;
extern const GUID IID_IFrame5_UWP;
extern const GUID IID_IFrameFactory_UWP;
extern const GUID IID_IFrameProj_UWP;
extern const GUID IID_IFrame_UWP;
extern const GUID IID_IFrameworkElement2_UWP;
extern const GUID IID_IFrameworkElement3_UWP;
extern const GUID IID_IFrameworkElement4_UWP;
extern const GUID IID_IFrameworkElement6_UWP;
extern const GUID IID_IFrameworkElement7_Proj;
extern const GUID IID_IFrameworkElement7_UWP;
extern const GUID IID_IFrameworkElementFactory_UWP;
extern const GUID IID_IFrameworkElementOverrides2_UWP;
extern const GUID IID_IFrameworkElementOverrides_UWP;
extern const GUID IID_IFrameworkElementStatics_UWP;
extern const GUID IID_IFrameworkElement_UWP;
extern const GUID IID_IGradientBrush_UWP;
extern const GUID IID_IGradientStop_UWP;
extern const GUID IID_IGrid3_Calc;
extern const GUID IID_IGrid3_UWP;
extern const GUID IID_IGridFactory_UWP;
extern const GUID IID_IGridStatics_UWP;
extern const GUID IID_IGridStatics_UWP2;
extern const GUID IID_IGrid_UWP;
extern const GUID IID_IGrid_UWP2;
extern const GUID IID_IHyperlinkButton_UWP;
extern const GUID IID_IImage2_UWP;
extern const GUID IID_IImageBrush_UWP;
extern const GUID IID_IItemsRepeaterSizingTarget_UWP;
extern const GUID IID_IItemsRepeater_UWP;
extern const GUID IID_IIterable_GradientStop_UWP;
extern const GUID IID_IIterator_GradientStop_UWP;
extern const GUID IID_IKeyRoutedEventArgs_UWP;
extern const GUID IID_IKeyboardAcceleratorFactory_UWP;
extern const GUID IID_IKeyboardAcceleratorInvokedEventArgs_UWP;
extern const GUID IID_IKeyboardAccelerator_UWP;
extern const GUID IID_ILinearGradientBrush_UWP;
extern const GUID IID_IListViewFactory_UWP;
extern const GUID IID_ILoadedImageSurfaceStatics_UWP;
extern const GUID IID_IMarkupExtensionFactory_UWP;
extern const GUID IID_IMarkupExtensionOverrides_UWP;
extern const GUID IID_IMarkupExtension_UWP;
extern const GUID IID_IMediaPlayerElement_UWP;
extern const GUID IID_IMenuBarItem_WinUI;
extern const GUID IID_IMenuBar_WinUI;
extern const GUID IID_IMenuFlyout2_Real_UWP;
extern const GUID IID_IMenuFlyoutItem3_UWP;
extern const GUID IID_IMenuFlyoutItemBaseFactory_UWP;
extern const GUID IID_IMenuFlyoutItemBase_UWP;
extern const GUID IID_IMenuFlyoutItemBase_UWP2;
extern const GUID IID_IMenuFlyoutItemFactory_UWP;
extern const GUID IID_IMenuFlyoutItem_UWP;
extern const GUID IID_IMenuFlyoutSeparatorFactory_UWP;
extern const GUID IID_IMenuFlyoutSubItem_UWP;
extern const GUID IID_IMenuFlyout_UWP;
extern const GUID IID_IMenuFlyout_UWP2;
extern const GUID IID_INavigate_UWP;
extern const GUID IID_INavigationTransitionInfo_UWP;
extern const GUID IID_INavigationView2_UWP;
extern const GUID IID_INavigationViewItem_UWP;
extern const GUID IID_INavigationView_UWP;
extern const GUID IID_IPageFactory_UWP;
extern const GUID IID_IPageOverrides_UWP;
extern const GUID IID_IPage_UWP;
extern const GUID IID_IPanelFactory_UWP;
extern const GUID IID_IPanelFactory_UWP2;
extern const GUID IID_IPanel_UWP;
extern const GUID IID_IPathGeometry_UWP;
extern const GUID IID_IPathGeometry_UWP_Alt;
extern const GUID IID_IPopup2_UWP;
extern const GUID IID_IPopup2_WinUI;
extern const GUID IID_IPopup3_UWP;
extern const GUID IID_IPopup3_WinUI;
extern const GUID IID_IPopup_UWP;
extern const GUID IID_IPopup_WinUI;
extern const GUID IID_IRadioButtonFactory_UWP;
extern const GUID IID_IRadioButton_UWP;
extern const GUID IID_IRadioButtons_WinUI;
extern const GUID IID_IRelativeSourceFactory_UWP;
extern const GUID IID_IResourceDictionary_UWP;
extern const GUID IID_IResourceDictionary_WinUI;
extern const GUID IID_IRoutedEventArgs2_UWP;
extern const GUID IID_IRoutedEventArgsFactory_UWP;
extern const GUID IID_IRoutedEventArgsFactory_WinUI3;
extern const GUID IID_IRoutedEventArgs_Clone_UWP;
extern const GUID IID_IRoutedEventArgs_QI_UWP;
extern const GUID IID_IRoutedEventArgs_WinUI3;
extern const GUID IID_IRowDefinition_UWP;
extern const GUID IID_IScrollViewer2_UWP;
extern const GUID IID_IScrollViewer3_UWP;
extern const GUID IID_IScrollViewer_UWP;
extern const GUID IID_ISetterBase_UWP;
extern const GUID IID_ISetter_UWP;
extern const GUID IID_ISolidColorBrush_UWP;
extern const GUID IID_ISplitView2_UWP;
extern const GUID IID_ISplitView_UWP;
extern const GUID IID_IStackPanel2_Real_UWP;
extern const GUID IID_IStackPanel_UWP;
extern const GUID IID_IStackPanel_UWP2;
extern const GUID IID_IStateTriggerBaseFactory2_UWP;
extern const GUID IID_IStateTriggerBaseFactory_UWP;
extern const GUID IID_IStateTriggerBaseProtected_UWP;
extern const GUID IID_IStateTriggerBase_UWP;
extern const GUID IID_IStyleSelectorFactory_UWP;
extern const GUID IID_IStyleSelectorFactory_UWP2;
extern const GUID IID_IStyle_UWP;
extern const GUID IID_ISwapChainPanelNative2_UWP;
extern const GUID IID_ISwapChainPanelNative_UWP;
extern const GUID IID_ISwapChainPanel_UWP;
extern const GUID IID_ITabItemViewModel;
extern const GUID IID_ITabView_WinUI;
extern const GUID IID_ITeachingTip_WinUI;
extern const GUID IID_ITextBlock2_UWP;
extern const GUID IID_ITextBlock3_UWP;
extern const GUID IID_ITextBlock6_UWP;
extern const GUID IID_ITextBlock_Calc2;
extern const GUID IID_ITextBlock_Calc3;
extern const GUID IID_ITextBlock_Calc4;
extern const GUID IID_ITextBlock_UWP;
extern const GUID IID_ITileBrush_UWP;
extern const GUID IID_ITileBrush_UWP_Alt;
extern const GUID IID_IToggleButtonFactory2_UWP;
extern const GUID IID_IToggleButtonFactory_UWP;
extern const GUID IID_IToggleButtonOverrides_UWP;
extern const GUID IID_IToggleButtonStatics_UWP;
extern const GUID IID_IToggleButton_UWP;
extern const GUID IID_IToggleMenuFlyoutItem_UWP;
extern const GUID IID_IUIE_Extra[107];
extern const GUID IID_IUIElement10_UWP;
extern const GUID IID_IUIElement2_UWP;
extern const GUID IID_IUIElement3_UWP;
extern const GUID IID_IUIElement4_Real_UWP;
extern const GUID IID_IUIElement4_UWP;
extern const GUID IID_IUIElement5_Real_UWP;
extern const GUID IID_IUIElement5_UWP;
extern const GUID IID_IUIElement7_UWP;
extern const GUID IID_IUIElementFactory_UWP;
extern const GUID IID_IUIElementOverrides_UWP;
extern const GUID IID_IUIElementStatics_UWP;
extern const GUID IID_IUIElement_Calc2;
extern const GUID IID_IUIElement_UWP;
extern const GUID IID_IUserControlFactory_UWP;
extern const GUID IID_IUserControl_UWP;
extern const GUID IID_IVectorView_GradientStop_UWP;
extern const GUID IID_IVector_GradientStop_UWP;
extern const GUID IID_IVisualStateGroup_UWP;
extern const GUID IID_IVisualStateManagerFactory_UWP;
extern const GUID IID_IVisualState_UWP;
extern const GUID IID_IVisualTreeHelperStatics_UWP;
extern const GUID IID_IXamlBindingHelperStatics_UWP;
extern const GUID IID_IXamlCompositionBrushBaseFactory_UWP;
extern const GUID IID_IXamlLightFactory_UWP;
extern const GUID IID_IXamlReaderStatics_UWP;
extern const GUID IID_IXamlRoot_UWP;
extern const GUID IID_IXamlUICommand_UWP;

#endif /* __WINE_WINDOWS_UI_XAML_UWP_GUIDS_H */
