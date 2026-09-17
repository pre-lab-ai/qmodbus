win32 {
	dlls.path = $$PREFIX
	qt6platforms.path = $${dlls.path}/platforms

	MAIN_DLLS=Qt6Core Qt6Gui Qt6Widgets Qt6SerialPort Qt6Sql
	for(dll, MAIN_DLLS): dlls.files += $$[QT_INSTALL_BINS]/$${dll}.dll

	# add required Qt plugin DLLs
	qt6platforms.files += $$[QT_INSTALL_PLUGINS]/platforms/qwindows.dll

	# Ship a reviewable configuration template with every release. Runtime
	# settings and acquisition data are written below AppDataLocation instead.
	release_config.path = $${dlls.path}/config
	release_config.files = $$PWD/data/point_table.json $$PWD/data/settings.template.ini

	INSTALLS += dlls qt6platforms release_config
}

unix {
	isEmpty(PREFIX) {
		PREFIX = /usr/local/bin
	}
}

target.path = $$PREFIX

INSTALLS += target
