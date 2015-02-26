/*=====================================================================
mainwin.cpp
-----------
File created by ClassTemplate on Sun Mar 13 21:12:40 2005

Copyright (C) Nicholas Chapman

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

http://www.gnu.org/copyleft/gpl.html.
=====================================================================*/
#include "mainwin.h"
#include "database.h"

#include "../utils/stringutils.h"
#include "CallstackView.h"
#include <wx/aui/auibook.h>
#include <wx/hashset.h>
#include <wx/menu.h>
#include <wx/filedlg.h>
#include <wx/gauge.h>
#include <set>
#include "../utils/except.h"

MainWin *theMainWin;

// IDs for the controls and the menu commands
enum
{
	// menu items
	MainWin_Quit = 1,
	MainWin_Open,
	MainWin_SaveAs,
	MainWin_ExportAsCsv,
	MainWin_LoadMinidumpSymbols,
	MainWin_View_Back,
	MainWin_View_Forward,
	MainWin_View_Collapse_OS,
	MainWin_View_Stats,
	MainWin_ResetToRoot,
	MainWin_Filters,
	MainWin_ResetFilters,

	// it is important for the id corresponding to the "About" command to have
	// this standard value as otherwise it won't be handled properly under Mac
	// (where it is special and put into the "Apple" menu)
	MainWin_About = wxID_ABOUT
};

MainWin::MainWin(const wxString& title, 
				 const std::wstring& profilepath,
				 Database *database)
				 :	wxFrame()
{
	assert(!theMainWin);
	theMainWin = this;
	wxString str;
	long style = wxDEFAULT_FRAME_STYLE;

	wxPoint pos(50, 50);
	wxSize size = wxGetDisplaySize();
	size.Scale(0.75f, 0.75f);

	if (config.Read("MainWinMaximized", 0L))
		style |= wxMAXIMIZE;
	pos.x = config.Read("MainWinX", (long)pos.x);
	pos.y = config.Read("MainWinY", (long)pos.x);
	size.x = config.Read("MainWinW", (long)size.x);
	size.y = config.Read("MainWinH", (long)size.y);

	Create(NULL, -1, title, pos, size, style);
				
	panel = NULL;
	proclist = NULL;
	sourceview = NULL;
	this->profilepath = profilepath;
	this->database = database;

	// set the frame icon
	SetIcon(sleepy_icon);

#if wxUSE_MENUS
	// create a menu bar
	wxMenu *menuFile = new wxMenu;
	menuFile->Append(MainWin_Open, _T("&Open..."), _T("Opens an existing profile"));
	menuFile->Append(MainWin_SaveAs, _T("Save &As..."), _T("Saves the profile data to a file"));
	menuFile->Append(MainWin_ExportAsCsv, _T("&Export as CSV..."), _T("Export the profile data to a CSV file"));
	menuFile->AppendSeparator();
	menuFile->Append(MainWin_LoadMinidumpSymbols,_T("Load symbols from &minidump"), _T("Loads symbols for modules recorded in the minidump included with this capture."))
		->Enable(database->has_minidump);
	menuFile->AppendSeparator();
	menuFile->Append(MainWin_Quit, _T("E&xit\tAlt-X"), _T("Quit this program"));

	// View options and layout.
	wxMenu *menuView = new wxMenu;
	menuView->Append(MainWin_View_Back,_T("Back\tAlt-Left"), _T("Go back to the previously-visited symbol"));
	menuView->Append(MainWin_View_Forward,_T("Forward\tAlt-Right"), _T("Go forward in history (undo \"Back\")"));
	menuView->AppendSeparator();
	menuView->Append(MainWin_View_Stats,_T("Show Profiling Statistics"), _T("Shows any extra information logged while profiling"));
	collapseOSCalls = menuView->AppendCheckItem(MainWin_View_Collapse_OS,_T("&Hide Collapsed Functions"), _T("Hide functions nested inside system calls")); 
	collapseOSCalls->Check(config.Read("MainWinCollapseOS",1)!=0);
	menuView->Append(MainWin_ResetToRoot , _T("Reset Profile &Root"), _T("Resets the root so that the entire profile is shown"));
	menuView->Append(MainWin_ResetFilters, _T("Reset Filters"), _T("Resets all the view filters"));

	// the "About" item should be in the help menu
	wxMenu *helpMenu = new wxMenu;
	helpMenu->Append(MainWin_About, _T("&About...\tF1"), _T("Show about dialog"));

	// now append the freshly created menu to the menu bar...
	wxMenuBar *menuBar = new wxMenuBar();
	menuBar->Append(menuFile, _T("&File"));
	menuBar->Append(menuView, _T("&View"));
	menuBar->Append(helpMenu, _T("&Help"));

	// ... and attach this menu bar to the frame
	SetMenuBar(menuBar);
#endif // wxUSE_MENUS

	CreateStatusBar(2);
	gauge = NULL;

	// Construct the docking panes
	wxSize clientSize = GetClientSize();

	aui = new wxAuiManager(this,wxAUI_MGR_RECTANGLE_HINT);

	wxWindow *splitWindow = new wxWindow(this,-1);

	sourceview = new SourceView(this ,this);

	// Create the windows
	proclist = new ProcList(this       , true , database);
	callers  = new ProcList(splitWindow, false, database);
	callees  = new ProcList(splitWindow, false, database);

	callStack = new CallstackView(this, database);

	aui->AddPane(proclist, wxAuiPaneInfo()
		.Name(wxT("Functions"))
		.CentrePane()
		.Caption(wxT("Functions"))
		.CaptionVisible(true)
		);

	sourceAndLog = new wxAuiNotebook(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxAUI_NB_TOP|wxAUI_NB_TAB_SPLIT|wxAUI_NB_TAB_MOVE|wxAUI_NB_SCROLL_BUTTONS|wxNO_BORDER);
	aui->AddPane(sourceAndLog, wxAuiPaneInfo()
		.Name(wxT("SourceAndLog"))
		.CaptionVisible(false)
		.CloseButton(false)
		.Bottom()
		.Layer(0)
		.BestSize(clientSize.GetWidth() * 2/3, clientSize.GetHeight() * 1/3)
		);

	// Set up the filters (search) view
	filters = new wxPropertyGrid(this, MainWin_Filters);

	filters->Append( new wxPropertyCategory("Main") );

	filters->Append( new wxStringProperty( "Function Name", "procname", "" ) );
	filters->Append( new wxStringProperty( "Module", "module", "" ) );
	filters->Append( new wxStringProperty( "Source File", "sourcefile", "" ) );

	sourceAndLog->AddPage(sourceview,wxT("Source"));
	log = new LogView(sourceAndLog);
	//wxTextCtrl *log = new wxTextCtrl(this, 0, "", wxDefaultPosition, wxSize(100,100), wxTE_MULTILINE|wxTE_READONLY);
	sourceAndLog->AddPage(log,wxT("Log"));

	callViews = new wxAuiNotebook(this,wxID_ANY,wxDefaultPosition,wxDefaultSize,wxAUI_NB_TOP|wxAUI_NB_TAB_SPLIT|wxAUI_NB_TAB_MOVE|wxAUI_NB_SCROLL_BUTTONS|wxNO_BORDER|wxNO_BORDER);

	callViews->AddPage(splitWindow,wxT("Averages"));
	callViews->AddPage(callStack,wxT("Call Stacks"));
	callViews->AddPage(filters,wxT("Filters"));
	aui->AddPane(callViews,wxAuiPaneInfo()
		.Name(wxT("CallInfo"))
		.CaptionVisible(false)
		.CloseButton(false)
		.Right()
		.Layer(1)
		.PaneBorder(false)
		.BestSize(clientSize.GetWidth() * 1/3, clientSize.GetHeight() * 1/3)
		);


	auiTab1 = new wxAuiManager(splitWindow,wxAUI_MGR_RECTANGLE_HINT);
	auiTab1->AddPane(callers, wxAuiPaneInfo()
		.Name(wxT("CalledFrom"))
		.Caption(wxT("Called From"))
		.CloseButton(false)
		.Center()
		.PaneBorder(false)
		);

	auiTab1->AddPane(callees, wxAuiPaneInfo()
		.Name("ChildCalls")
		.Caption(wxT("Child Calls"))
		.CloseButton(false)
		.Center()
		.PaneBorder(false)
		);

	aui->Update();
	auiTab1->Update();

	// Calculate a string with the content of the AUI.
	// If we add or remove elements, the config will reset.
	contentString = aui->SavePerspective()+"|"+auiTab1->SavePerspective();

	if(config.Read("MainWinContent") == contentString) {
		if (config.Read("MainWinLayout", &str) ) {
			aui->LoadPerspective(str);
		}
		if (config.Read("MainWinBookTab1Layout", &str) ) {
			auiTab1->LoadPerspective(str);
		}
		callViews->SetSelection(config.Read("MainWinBookTab",callViews->GetSelection()));
	}

	aui->Update();
	proclist->SetFocus();

	filters->FitColumns();
}

WX_DECLARE_HASH_SET(wxString, wxStringHash, wxStringEqual, wxStringHashSet);

static void addSplitValues(wxStringHashSet& dest, const std::wstring& str, wchar_t sep)
{
	std::wistringstream ss(str);
	std::wstring item;
	while (std::getline(ss, item, sep))
		dest.insert(item);
}

static wxArrayString arrayFromSet( const wxStringHashSet& set )
{
	wxArrayString dest;
	dest.reserve(set.size());

	for (auto iter = set.begin(); iter != set.end(); ++iter)
	{
		if( *iter != "" )
			dest.Add(*iter);
	}

	return dest;
}

void MainWin::buildFilterAutocomplete()
{
	wxStringHashSet procnameAutocomplete;
	wxStringHashSet moduleAutocomplete;
	wxStringHashSet sourcefileAutocomplete;

	setProgress(L"Collecting autocomplete data...", database->getSymbolCount());

	for (Database::Symbol::ID id = 0; id < database->getSymbolCount(); id++)
	{
		const Database::Symbol *symbol = database->getSymbol(id);
		procnameAutocomplete.insert(symbol->procname);

		addSplitValues(procnameAutocomplete, symbol->procname, ':');

		updateProgress(id);
	}

	for (Database::FileID id = 0; id < database->getFileCount(); id++)
	{
		const std::wstring &filename = database->getFileName(id);
		sourcefileAutocomplete.insert(filename);
		addSplitValues(sourcefileAutocomplete, filename, '\\');
	}

	for (Database::ModuleID id = 0; id < database->getModuleCount(); id++)
		moduleAutocomplete.insert(database->getModuleName(id));

	setProgress(L"Applying autocomplete data...");

	filters->SetPropertyAttribute("procname"  , "AutoComplete", arrayFromSet(procnameAutocomplete));
	filters->SetPropertyAttribute("module"    , "AutoComplete", arrayFromSet(moduleAutocomplete));
	filters->SetPropertyAttribute("sourcefile", "AutoComplete", arrayFromSet(sourcefileAutocomplete));

	setProgress(NULL);
}

MainWin::~MainWin()
{
	auiTab1->UnInit();
	aui->UnInit();
	delete database;
	delete auiTab1;
	delete aui;
}


// the event tables connect the wxWindows events with the functions (event
// handlers) which process them. It can be also done at run-time, but for the
// simple menu events like this the static method is much simpler.
BEGIN_EVENT_TABLE(MainWin, wxFrame)
EVT_CLOSE(MainWin::OnClose)
EVT_MENU(MainWin_Quit,  MainWin::OnQuit)
EVT_MENU(MainWin_Open,  MainWin::OnOpen)
EVT_MENU(MainWin_SaveAs,  MainWin::OnSaveAs)
EVT_MENU(MainWin_ExportAsCsv,  MainWin::OnExportAsCsv)
EVT_MENU(MainWin_LoadMinidumpSymbols,  MainWin::OnLoadMinidumpSymbols)
EVT_MENU(MainWin_View_Back, MainWin::OnBack)
EVT_UPDATE_UI(MainWin_View_Back, MainWin::OnBackUpdate)
EVT_MENU(MainWin_View_Forward, MainWin::OnForward)
EVT_UPDATE_UI(MainWin_View_Forward, MainWin::OnForwardUpdate)
EVT_MENU(MainWin_ResetToRoot, MainWin::OnResetToRoot)
EVT_UPDATE_UI(MainWin_ResetToRoot, MainWin::OnResetToRootUpdate)
EVT_MENU(MainWin_ResetFilters, MainWin::OnResetFilters)
EVT_MENU(MainWin_View_Collapse_OS,  MainWin::OnCollapseOS)
EVT_MENU(MainWin_View_Stats,  MainWin::OnStats)
EVT_MENU(MainWin_About, MainWin::OnAbout)	
EVT_PG_CHANGED(MainWin_Filters, MainWin::OnFiltersChanged)
END_EVENT_TABLE()

void MainWin::OnClose(wxCloseEvent& WXUNUSED(event))
{
	config.Write("MainWinMaximized", IsMaximized());

	assert(GetScreenRect().width > 200);

	wxString str = aui->SavePerspective();
	config.Write("MainWinLayout", str);
	config.Write("MainWinX", GetScreenRect().x);
	config.Write("MainWinY", GetScreenRect().y);
	config.Write("MainWinW", GetScreenRect().width);
	config.Write("MainWinH", GetScreenRect().height);
	config.Write("MainWinBookTab",callViews->GetSelection());
	config.Write("MainWinBookTab1Layout",auiTab1->SavePerspective());
	config.Write("MainWinContent",contentString);
	config.Write("MainWinCollapseOS",collapseOSCalls->IsChecked());

	wxExit();
}

void MainWin::OnQuit(wxCommandEvent& WXUNUSED(event))
{
	wxExit();
}

void MainWin::OnOpen(wxCommandEvent& WXUNUSED(event))
{
	wxString filename = ProfilerGUI::PromptOpen(this);
	if (filename.empty())
		return;

	try
	{
		database->loadFromPath(filename.c_str().AsWChar(), collapseOSCalls->IsChecked(), false);

		SetTitle(wxString::Format("%s - %s", APPNAME, filename.c_str()));
	}
	catch (SleepyException &e)
	{
		wxLogError("%ls\n", e.wwhat());
		clear();
	}

	reset();
}

void MainWin::OnSaveAs(wxCommandEvent& WXUNUSED(event))
{
	wxFileDialog dlg(this, "Save File As", "", "capture.sleepy", _T(APPNAME) L" Profiles (*.sleepy)|*.sleepy", 
		wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_CANCEL)
	{
		if (!CopyFile(profilepath.c_str(), dlg.GetPath(), FALSE))
		{
			wxLogSysError("Could not save profile data.\n");
		}
		else
		{
			SetTitle(wxString::Format("%s - %s", APPNAME, dlg.GetPath()));
		}
	}
}

void MainWin::OnExportAsCsv(wxCommandEvent& WXUNUSED(event))
{
	wxFileDialog dlg(this, "Export File As", "", "capture.csv", "CSV Files (*.csv)|*.csv", 
		wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_CANCEL)
	{
		wxFileOutputStream file(dlg.GetPath());
		if(!file.IsOk())
			wxLogSysError("Could not export profile data.\n");
		wxTextOutputStream txt(file);
		for each (const Database::Item &item in database->getMainList().items)
		{
			writeQuote(txt, item.symbol->procname, '"'); txt << ",";
			txt << item.exclusive << ",";
			txt << item.inclusive << ",";
			txt << (item.exclusive*100.0f/database->getMainList().totalcount) << ",";
			txt << (item.inclusive*100.0f/database->getMainList().totalcount) << ",";
			writeQuote(txt, database->getModuleName(item.symbol->module), '"'); txt << ",";
			writeQuote(txt, database->getFileName(item.symbol->sourcefile), '"'); txt << ",";
			txt << database->getAddrInfo(item.symbol->address)->sourceline << "\n";
		}
	}
}

void MainWin::OnLoadMinidumpSymbols( wxCommandEvent& event )
{
	// Open the log tab, so the user sees output from the debug engine.
	sourceAndLog->SetSelection(1);

	// Symbols loaded from the minidump persist across reload calls.
	// Thus, we need to call reload with loadMinidump==true only once
	// (as opposed to remembering whether we want to see minidump symbols).
	reload(true);

	symbolsChanged();
	refresh();
}

void MainWin::OnBack(wxCommandEvent& event)
{
	historyPos--;
	inspectSymbol(database->getAddrInfo(history[historyPos])->symbol, false);
}

void MainWin::OnBackUpdate(wxUpdateUIEvent& event)
{
	event.Enable(historyPos > 0);
}

void MainWin::OnForward(wxCommandEvent& event)
{
	historyPos++;
	inspectSymbol(database->getAddrInfo(history[historyPos])->symbol, false);
}

void MainWin::OnForwardUpdate(wxUpdateUIEvent& event)
{
	event.Enable(historyPos+1 < history.size());
}

void MainWin::OnResetToRoot(wxCommandEvent& WXUNUSED(event))
{
	database->setRoot(NULL);
	Refresh();
}

void MainWin::OnResetToRootUpdate(wxUpdateUIEvent& event)
{
	event.Enable(database->getRoot() != NULL);
}

void MainWin::OnResetFilters(wxCommandEvent& event)
{
	filters->GetProperty("procname"  )->SetValueFromString("");
	filters->GetProperty("module"    )->SetValueFromString("");
	filters->GetProperty("sourcefile")->SetValueFromString("");
	applyFilters();
	refresh();
}

void MainWin::OnCollapseOS(wxCommandEvent& event)
{
	reload();
	refresh();
}

void MainWin::OnStats(wxCommandEvent& event)
{
	wxDialog dlg(this, -1, wxString("Statistics"), wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER|wxDEFAULT_DIALOG_STYLE);
	wxSizer *sizer = new wxBoxSizer(wxVERTICAL);

	wxString string;
	string += database->getProfilePath();
	string += "\n";
	string += "\n";

	for (size_t n=0;n<database->stats.size();n++)
	{
		string += database->stats[n];
		string += "\n";
	}

	wxTextCtrl *text = new wxTextCtrl(&dlg, wxID_ANY, string, wxDefaultPosition, wxDefaultSize, 
		wxBORDER_NONE|wxTE_READONLY|wxTE_MULTILINE|wxTE_NO_VSCROLL);
	text->SetBackgroundColour(dlg.GetBackgroundColour());
	sizer->Add(text, wxSizerFlags().Expand().Proportion(1).Border(wxALL, 10));

	wxSizer *sizerBtns = dlg.CreateButtonSizer(wxOK);
	if ( sizerBtns )
	{
		sizer->Add(sizerBtns, wxSizerFlags().Expand().Border());
	}

	dlg.SetSizerAndFit(sizer);
	dlg.SetSize(300, 200);
	dlg.CentreOnScreen();
	dlg.ShowModal();
}

void MainWin::OnAbout(wxCommandEvent& WXUNUSED(event))
{
	ProfilerGUI::ShowAboutBox();
}

void MainWin::OnFiltersChanged(wxPropertyGridEvent& event)
{
	applyFilters();
	refresh();
}

//////////////////////////////////////////////////////////////////////////

void MainWin::clear()
{
	database->clear();
	SetTitle(wxString::Format("%s", APPNAME));
}

void MainWin::reload(bool loadMinidump/*=false*/)
{
	try
	{
		database->reload(collapseOSCalls->IsChecked(), loadMinidump);
	}
	catch (SleepyException &e)
	{
		wxLogError("%ls\n", e.wwhat());
		clear();
	}
}

void MainWin::showSource( const Database::Symbol * symbol )
{
	if (sourceAndLog->GetSelection() != 0)
		sourceAndLog->SetSelection(0); // Open source tab

	std::vector<double> linecounts = database->getLineCounts(symbol->sourcefile);

	if (symbol->procname == L"KiFastSystemCallRet")
		sourceview->showFile(L"[hint KiFastSystemCallRet]", 0, std::vector<double>());
	else
		sourceview->showFile(database->getFileName(symbol->sourcefile), database->getAddrInfo(symbol->address)->sourceline, linecounts);
}

void MainWin::focusSymbol(const Database::Symbol *symbol)
{
	showSource(symbol);
	proclist->focusSymbol(symbol);
	callers->focusSymbol(symbol);
	callees->focusSymbol(symbol);
	//callStack->focusSymbol(symbol);
}

void MainWin::inspectSymbol(const Database::Symbol *symbol, bool addtohistory/*=true*/)
{
	showSource(symbol);
	proclist->focusSymbol(symbol);
	callers->showList(database->getCallers(symbol));
	callees->showList(database->getCallees(symbol));
	callStack->showCallStack(symbol);

	if (addtohistory && symbol)
	{
		if (history.empty())
			history.push_back(symbol->address);
		else
		if (history[historyPos] != symbol->address)
		{
			history.resize(historyPos+1);
			history.push_back(symbol->address);
			historyPos++;
		}
		assert(historyPos==history.size()-1);
	}
}

void MainWin::reset()
{
	viewstate.highlighted.clear();
	viewstate.filtered.clear();
	history.clear();
	historyPos = 0;

	symbolsChanged();
	refresh();
}

void MainWin::symbolsChanged()
{
	buildFilterAutocomplete();
	applyFilters();
}

void MainWin::refresh()
{
	const Database::Symbol *symbol = proclist->getFocusedSymbol();
	proclist->showList(database->getMainList());
	callers->showList(database->getCallers(symbol));
	callees->showList(database->getCallees(symbol));
	callStack->showCallStack(symbol);
}

void MainWin::setSourcePos(const std::wstring& currentfile_, int currentline_)
{
	if(currentfile != currentfile_ || currentline != currentline_)
	{
		currentfile = currentfile_;
		currentline = currentline_;

		updateStatusBar();
	}
}

void MainWin::updateStatusBar()
{
	SetStatusText(std::wstring("Source file: " + currentfile).c_str(), 0);
	SetStatusText(std::wstring("Line " + ::toString(currentline)).c_str(), 1);
}

void MainWin::applyFilters()
{
	std::wstring filter_procname   = filters->GetProperty("procname"  )->GetValueAsString();
	std::wstring filter_module     = filters->GetProperty("module"    )->GetValueAsString();
	std::wstring filter_sourcefile = filters->GetProperty("sourcefile")->GetValueAsString();

	for (Database::Symbol::ID id = 0; id < database->getSymbolCount(); id++)
	{
		const Database::Symbol *symbol = database->getSymbol(id);

		// TODO: Filter module and file names only once
		bool filtered =
			( !filter_procname  .empty() &&                         symbol->procname   .find(filter_procname  ) == std::wstring::npos ) ||
			( !filter_module    .empty() && database->getModuleName(symbol->module    ).find(filter_module    ) == std::wstring::npos ) ||
			( !filter_sourcefile.empty() && database->getFileName  (symbol->sourcefile).find(filter_sourcefile) == std::wstring::npos );

		set_set(viewstate.filtered, symbol->address, filtered);
	}
}

void MainWin::setFilter(const wxString &name, const wxString &value)
{
	filters->GetProperty(name)->SetValueFromString(value);
	applyFilters();
	refresh();
}

void MainWin::setHighlight(const std::vector<Database::Address> &addresses, bool set)
{
	for each (Database::Address address in addresses)
		set_set(viewstate.highlighted, address, set);
	refresh();
}

void MainWin::setProgress(const wchar_t *text, int max)
{
	if (text)
	{
		if (!gauge)
		{
			wxStatusBar *statusBar = GetStatusBar();
			gauge = new wxGauge(statusBar, -1, 0xFFFF, wxDefaultPosition, wxDefaultSize, wxGA_HORIZONTAL|wxGA_SMOOTH);

			wxRect gaugeRect;
			statusBar->GetFieldRect(1, gaugeRect);
			static const int margin = 2;
			gauge->SetPosition(wxPoint(gaugeRect.x+margin, gaugeRect.y+margin));
			gauge->SetSize(wxSize(gaugeRect.width-2*margin, gaugeRect.height-2*margin));
		}

		SetStatusText(text, 0);
		SetStatusText("", 1);
		if (max)
		{
			gauge->SetValue(0);
			gauge->SetRange(max);
		}
		else
			gauge->Pulse();
	}
	else
	{
		if (gauge)
		{
			delete gauge;
			gauge = NULL;
		}

		updateStatusBar();
	}
}

void MainWin::updateProgress(int pos)
{
	assert(gauge);
	gauge->SetValue(pos);
}
