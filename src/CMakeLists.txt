cmake_minimum_required(VERSION 3.16)

find_package(mo2-cmake CONFIG REQUIRED)

if (UNIX)
	find_package(usvfs-fuse CONFIG REQUIRED)
	find_package(unofficial-breakpad CONFIG REQUIRED)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(libsecret REQUIRED IMPORTED_TARGET libsecret-1)
	set(OS_SPECIFIC_PRIVATE_DEPS
			usvfs-fuse
			unofficial::breakpad::libbreakpad unofficial::breakpad::libbreakpad_client
			PkgConfig::libsecret
	)
	set(LIBRARY_DIR lib)
	set(os_name linux)
else()
	find_package(usvfs CONFIG REQUIRED)
	set(OS_SPECIFIC_PRIVATE_DEPS Shlwapi Bcrypt Version Dbghelp usvfs::usvfs)
	set(LIBRARY_DIR dlls)
	set(os_name win32)
endif()

find_package(mo2-uibase CONFIG REQUIRED)
find_package(mo2-archive CONFIG REQUIRED)
find_package(mo2-lootcli-header CONFIG REQUIRED)
find_package(mo2-bsatk CONFIG REQUIRED)
find_package(mo2-esptk CONFIG REQUIRED)
find_package(libbsarchpp CONFIG REQUIRED)

find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets WebSockets)
find_package(Boost CONFIG REQUIRED COMPONENTS program_options thread interprocess signals2 uuid accumulators)
find_package(7zip CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(organizer)
set_target_properties(organizer PROPERTIES
	OUTPUT_NAME "ModOrganizer"
	WIN32_EXECUTABLE TRUE)

configure_file(version.h.in version.h)
configure_file(version.rc.in version.rc)

target_include_directories(organizer PUBLIC ${CMAKE_SOURCE_DIR}/src)

# required for files configured using `configure_file`
target_include_directories(organizer PRIVATE ${CMAKE_BINARY_DIR}/src)

# required for os-specific ui files
set_property(TARGET organizer APPEND PROPERTY AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/src/${os_name})

# disable translations because we want to be able to install somewhere else if
# required
mo2_configure_target(organizer WARNINGS 4 TRANSLATIONS OFF)

if(WIN32)
	# add configured version.rc to sources
	target_sources(organizer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()

if(UNIX)
	set_target_properties(organizer PROPERTIES INSTALL_RPATH ".:./lib/")
endif()

# we add translations "manually" to handle MO2_INSTALL_IS_BIN
mo2_add_translations(organizer
	INSTALL_RELEASE
	INSTALL_DIRECTORY "${_bin}/translations"
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

mo2_set_project_to_run_from_install(
	organizer EXECUTABLE ${CMAKE_INSTALL_PREFIX}/${_bin}/ModOrganizer.exe)

target_link_libraries(organizer PRIVATE
	mo2::uibase mo2::archive libbsarchpp
	mo2::bsatk mo2::esptk mo2::lootcli-header
	Boost::program_options Boost::signals2 Boost::uuid Boost::accumulators
	Qt6::WebEngineWidgets Qt6::WebSockets
	${OS_SPECIFIC_PRIVATE_DEPS}
)

if (WIN32)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/dlls.manifest.qt6"
			DESTINATION ${_bin}/dlls
			CONFIGURATIONS Release RelWithDebInfo
			RENAME dlls.manifest)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/dlls.manifest.debug.qt6"
			DESTINATION ${_bin}/dlls
			CONFIGURATIONS Debug
			RENAME dlls.manifest)
endif()

if (NOT MO2_SKIP_STYLESHEETS_INSTALL)
	install(
		DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/stylesheets"
		DESTINATION ${_bin})
endif()

if (NOT MO2_SKIP_TUTORIALS_INSTALL)
	install(
		DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tutorials"
		DESTINATION ${_bin})
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/markdown.html"
	DESTINATION ${_bin}/resources)

# install ModOrganizer.exe itself
install(TARGETS organizer DESTINATION ${_bin})

# install dependencies DLLs
install(FILES $<TARGET_FILE:libbsarchpp> DESTINATION ${_bin}/${LIBRARY_DIR})
install(FILES $<TARGET_FILE:7zip::7zip> DESTINATION ${_bin}/${LIBRARY_DIR})

# this may copy over the ones from uibase/usvfs
# - when building with mob, this should not matter as the files should be identical
# - when building standalone, this should help having matching USVFS DLL between the
#   build and the installation
# - this may cause issue with uibase in standalone mode if the installed version does
#   not match the one used for the build, but there would be other issue anyway (e.g.
#   different uibase.dll between modorganizer and plugins)
#
install(FILES
	$<TARGET_FILE:mo2::uibase>
DESTINATION ${_bin})

if(WIN32)
	install(FILES
			$<TARGET_FILE:usvfs_x64::usvfs_dll>
			$<TARGET_FILE:usvfs_x86::usvfs_dll>
			$<TARGET_FILE:usvfs_x64::usvfs_proxy>
			$<TARGET_FILE:usvfs_x86::usvfs_proxy>
			DESTINATION ${_bin})
endif()

# do not install PDB if CMAKE_INSTALL_PREFIX is "bin"
if (NOT MO2_INSTALL_IS_BIN)
	mo2_install_pdb(TARGET organizer)
endif()

if (WIN32)
	mo2_deploy_qt(
		DIRECTORY ${_bin}
		BINARIES ModOrganizer.exe $<TARGET_FILE_NAME:mo2::uibase>)
endif()

# set source groups for VS
mo2_add_filter(NAME src/application GROUPS
	iuserinterface
	commandline
	${os_name}/commandline_${os_name}
	${os_name}/main
	moapplication
	${os_name}/moapplication_${os_name}
	moshortcut
	multiprocess
	${os_name}/multiprocess_${os_name}
	sanitychecks
	${os_name}/sanitychecks_${os_name}
	selfupdater
	systemtraymanager
	updatedialog
)

mo2_add_filter(NAME src/browser GROUPS
	browserdialog
	browserview
)

mo2_add_filter(NAME src/categories GROUPS
	categories
	categoriestable
	categoriesdialog
	categoryimportdialog
)

mo2_add_filter(NAME src/core GROUPS
	archivefiletree
	githubpp
	installationmanager
	nexusinterface
	nxmaccessmanager
	organizercore
	game_features
	plugincontainer
	apiuseraccount
	processrunner
	${os_name}/processrunner_${os_name}
	qdirfiletree
	virtualfiletree
	uilocker
)

mo2_add_filter(NAME src/dialogs GROUPS
	aboutdialog
	activatemodsdialog
	credentialsdialog
	filedialogmemory
	forcedloaddialog
	forcedloaddialogwidget
	listdialog
	messagedialog
	motddialog
	overwriteinfodialog
	queryoverwritedialog
	problemsdialog
	savetextasdialog
	selectiondialog
	syncoverwritedialog
	transfersavesdialog
	waitingonclosedialog
)

mo2_add_filter(NAME src/downloads GROUPS
	downloadlist
	downloadlistview
	downloadmanager
)

mo2_add_filter(NAME src/env GROUPS
	env
	${os_name}/env_${os_name}
	envdump
	envfs
	envmetrics
	envmodule
	${os_name}/envmodule
	envsecurity
	${os_name}/envsecurity
	${os_name}/envshell
	envshortcut
	${os_name}/envshortcut_${os_name}
	envos
	${os_name}/envos_${os_name}
)

mo2_add_filter(NAME src/executables GROUPS
	executableslist
	${os_name}/executableslist_${os_name}
	editexecutablesdialog
	${os_name}/editexecutablesdialog_${os_name}
)

mo2_add_filter(NAME src/instances GROUPS
	createinstancedialog
	${os_name}/createinstancedialog
	createinstancedialogpages
	instancemanager
	${os_name}/instancemanager_${os_name}
	instancemanagerdialog
	${os_name}/instancemanagerdialog_${os_name}
)

mo2_add_filter(NAME src/loot GROUPS
	loot
	${os_name}/loot
	lootdialog
)

mo2_add_filter(NAME src/mainwindow GROUPS
	datatab
	downloadstab
	iconfetcher
	filetree
	${os_name}/filetree_${os_name}
	filetreeitem
	filetreemodel
	mainwindow
	savestab
	statusbar
)

mo2_add_filter(NAME src/modinfo GROUPS
	modinfo
	modinfobackup
	modinfoforeign
	modinfooverwrite
	modinforegular
	modinfoseparator
	modinfowithconflictinfo
)

mo2_add_filter(NAME src/modinfo/dialog GROUPS
	modinfodialog
	modinfodialogcategories
	modinfodialogconflicts
	modinfodialogconflictsmodels
	modinfodialogesps
	modinfodialogfiletree
	modinfodialogfwd
	modinfodialogimages
	modinfodialognexus
	modinfodialogtab
	modinfodialogtextfiles
)

mo2_add_filter(NAME src/modinfo/dialog/widgets GROUPS
	modidlineedit
)

mo2_add_filter(NAME src/modlist GROUPS
	modlist
	modlistdropinfo
	modlistsortproxy
	modlistbypriorityproxy
)

mo2_add_filter(NAME src/modlist/view GROUPS
	filterlist
	modlistview
	modlistviewactions
	modlistcontextmenu
	modflagicondelegate
	modcontenticondelegate
	modconflicticondelegate
	modlistversiondelegate
)

mo2_add_filter(NAME src/plugins GROUPS
	pluginlist
	${os_name}/pluginlist_${os_name}
	pluginlistsortproxy
	pluginlistview
	pluginlistcontextmenu
)

mo2_add_filter(NAME src/previews GROUPS
	previewdialog
	previewgenerator
)

mo2_add_filter(NAME src/profiles GROUPS
	profile
	${os_name}/profile_${os_name}
	profileinputdialog
	profilesdialog
)

mo2_add_filter(NAME src/proxies GROUPS
	downloadmanagerproxy
	executableslistproxy
	gamefeaturesproxy
	instancemanagerproxy
	modlistproxy
	organizerproxy
	pluginlistproxy
	proxyutils
)

mo2_add_filter(NAME src/register GROUPS
	shared/directoryentry
	shared/fileentry
	shared/filesorigin
	shared/fileregister
	shared/fileregisterfwd
	shared/originconnection
	directoryrefresher
)

mo2_add_filter(NAME src/settings GROUPS
	settings
	${os_name}/settings_${os_name}
	settingsutilities
	${os_name}/settingsutilities_${os_name}
)

mo2_add_filter(NAME src/settingsdialog GROUPS
	settingsdialog
	settingsdialogdiagnostics
	settingsdialoggeneral
	settingsdialognexus
	settingsdialogpaths
	settingsdialogplugins
	settingsdialogworkarounds
	settingsdialogmodlist
	settingsdialogtheme
	disableproxyplugindialog
)

mo2_add_filter(NAME src/utilities GROUPS
	shared/appconfig
	bbcode
	csvbuilder
	persistentcookiejar
	serverinfo
	spawn
	${os_name}/spawn_${os_name}
	shared/util
	shared/${os_name}/util_${os_name}
	shared/nativeString
	${os_name}/usvfsconnector
	shared/os_error
	shared/${os_name}/os_error
	thread_utils
	json
	glob_matching
)

mo2_add_filter(NAME src/widgets GROUPS
	colortable
	genericicondelegate
	filerenamer
	icondelegate
	lcdnumber
	loglist
	loghighlighter
	noeditdelegate
	qtgroupingproxy
	texteditor
	viewmarkingscrollbar
	modelutils
	copyeventfilter
)
