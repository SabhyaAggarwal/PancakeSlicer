#ifndef slic3r_GUI_PRE_PRINT_CHECK_hpp_
#define slic3r_GUI_PRE_PRINT_CHECK_hpp_

#include <functional>
#include <wx/string.h>
#include <vector>
#include <string>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/wx.h>
#include "Widgets/Label.hpp"
namespace Slic3r { namespace GUI {

enum prePrintInfoLevel {
    Normal,
    Warning,
    Error
};

enum prePrintInfoType {
    Printer,
    Filament
};

struct prePrintInfo
{
    prePrintInfoLevel level;
    prePrintInfoType  type;
    wxString msg;
    wxString tips;
    wxString wiki_url;
    wxString              link_label;
    std::function<void()> link_callback;
    wxString                  checkbox_label;
    bool                      checkbox_checked{false};
    std::function<void(bool)> checkbox_callback;
    int index{0};

public:
    bool operator==(const prePrintInfo& other) const {
        return level == other.level && type == other.type &&
               msg == other.msg && tips == other.tips &&
               wiki_url == other.wiki_url && link_label == other.link_label &&
               checkbox_label == other.checkbox_label && checkbox_checked == other.checkbox_checked &&
               index == other.index;
    }
};

enum PrintDialogStatus : unsigned int {

    PrintStatusErrorBegin,
    PrintStatusPrinterErrorBegin,
    PrintStatusInit,
    PrintStatusNoUserLogin,
    PrintStatusInvalidPrinter,
    PrintStatusConnectingServer,
    PrintStatusReadingTimeout,
    PrintStatusReading,
    PrintStatusConnecting,
    PrintStatusReconnecting,
    PrintStatusInUpgrading,
    PrintStatusFirmwareNotSupportTpuAtLeft,
    PrintStatusModeNotFDM,
    PrintStatusInSystemPrinting,
    PrintStatusInPrinting,
    PrintStatusNozzleMatchInvalid,
    PrintStatusNozzleDataInvalid,
    PrintStatusNozzleTypeMismatch,
    PrintStatusNozzleNoMatchedHotends,
    PrintStatusNozzleRackMaximumInstalled,
    PrintStatusRefreshingMachineList,
    PrintStatusSending,
    PrintStatusLanModeNoSdcard,
    PrintStatusNoSdcard,
    PrintStatusLanModeSDcardNotAvailable,
    PrintStatusNeedForceUpgrading,
    PrintStatusNeedConsistencyUpgrading,
    PrintStatusNotSupportedPrintAll,
    PrintStatusBlankPlate,
    PrintStatusUnsupportedPrinter,
    PrintStatusRackNozzleMappingWaiting,
    PrintStatusRackNozzleMappingError,
    PrintStatusRackReading,
    PrintStatusFilaSwitcherError,
    PrintStatusPrinterErrorEnd,

    PrintStatusFilamentErrorBegin,
    PrintStatusAmsOnSettingup,
    PrintStatusAmsMappingInvalid,
    PrintStatusAmsMappingU0Invalid,
    PrintStatusAmsMappingMixInvalid,
    PrintStatusTPUUnsupportAutoCali,
    PrintStatusHasFilamentInBlackListError,
    PrintStatusColorQuantityExceed,
    PrintStatusFilamentErrorEnd,

    PrintStatusErrorEnd,


    PrintStatusWarningBegin,
    PrintStatusPrinterWarningBegin,
    PrintStatusTimelapseNoSdcard,
    PrintStatusTimelapseWarning,
    PrintStatusTimelapseStorageLow,
    PrintStatusMixAmsAndVtSlotWarning,
    PrintStatusToolHeadCoolingFanWarning,
    PrintStatusRackNozzleMappingWarning,
    PrintStatusFilaSwitcherSlicingNotMatch,
    PrintStatusRackNozzleNumUnmeetWarning,
    PrintStatusHasUnreliableNozzleWarning,
    PrintStatusNozzleDiameterMismatch,
    PrintStatusOptionalPrinterModel,
    PrintStatusPrinterWarningEnd,

    PrintStatusFilamentWarningBegin,
    PrintStatusWarningKvalueNotUsed,
    PrintStatusHasFilamentInBlackListWarning,
    PrintStatusFilamentWarningHighChamberTemp,
    PrintStatusFilamentWarningHighChamberTempCloseDoor,
    PrintStatusFilamentWarningHighChamberTempSoft,
    PrintStatusFilamentWarningUnknownHighChamberTempSoft,
    PrintStatusWarningExtFilamentNotMatch,
    PrintStatusFilamentWarningNozzleHRC,
    PrintStatusFilamentCrossExtruderWarning,
    PrintStatusTPUUnsupportCaliOn,
    PrintStatusTPUUnsuggestCali,
    PrintStatusSmartNozzleBlobNeedAuto,
    PrintStatusFilamentWarningRemainNotEnough,
    PrintStatusPrintTimeEstimateWarning,
    PrintStatusFilamentWarningEnd,

    PrintStatusWarningEnd,
    PrintStatusReadingFinished,
    PrintStatusSendingCanceled,
    PrintStatusReadyToGo,

    PrintStatusAmsMappingSuccess,

    PrintStatusNotOnTheSameLAN,
    PrintStatusNotSupportedSendToSDCard,
    PrintStatusPublicInitFailed,
    PrintStatusPublicUploadFiled,
};

class PrePrintChecker
{
public:
    std::vector<prePrintInfo> printerList;
    std::vector<prePrintInfo> filamentList;

public:
    void clear();
    void add(PrintDialogStatus state, wxString msg, wxString tip, const wxString& wiki_url);
    void add_with_link(PrintDialogStatus state, wxString msg, wxString link_label, std::function<void()> link_callback);
    void add_with_checkbox(PrintDialogStatus state, wxString msg, wxString checkbox_label, bool checked, std::function<void(bool)> checkbox_callback);
    static ::std::string get_print_status_info(PrintDialogStatus status);

	static wxString get_pre_state_msg(PrintDialogStatus status);
    static bool is_error(PrintDialogStatus status) { return (PrintStatusErrorBegin < status) && (PrintStatusErrorEnd > status); };
    static bool is_error_printer(PrintDialogStatus status) { return (PrintStatusPrinterErrorBegin < status) && (PrintStatusPrinterErrorEnd > status); };
    static bool is_error_filament(PrintDialogStatus status) { return (PrintStatusFilamentErrorBegin < status) && (PrintStatusFilamentErrorEnd > status); };
    static bool is_warning(PrintDialogStatus status) { return (PrintStatusWarningBegin < status) && (PrintStatusWarningEnd > status); };
    static bool is_warning_printer(PrintDialogStatus status) { return (PrintStatusPrinterWarningBegin < status) && (PrintStatusPrinterWarningEnd > status); };
    static bool is_warning_filament(PrintDialogStatus status) { return (PrintStatusFilamentWarningBegin < status) && (PrintStatusFilamentWarningEnd > status); };
};



class PrinterMsgPanel : public wxPanel
{
public:
    PrinterMsgPanel(wxWindow *parent);

public:
    bool  UpdateInfos(const std::vector<prePrintInfo>& infos);

 private:
    wxBoxSizer*  m_sizer = nullptr;
    std::vector<prePrintInfo> m_infos;

};


}}

#endif
