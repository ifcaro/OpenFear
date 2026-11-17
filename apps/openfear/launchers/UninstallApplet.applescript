-- Compiled into "Uninstall OpenFear.app" via osacompile (cmake/OpenFearMacOS.cmake).
-- No "on open" handler - --uninstall takes no path, nothing is ever
-- dragged onto this one, unlike the installer.
on run
	set myDir to do shell script "dirname " & quoted form of (POSIX path of (path to me))
	set binPath to myDir & "/OpenFear.app/Contents/MacOS/openfear"
	-- openfear itself already shows its own dialog for every outcome - a
	-- non-zero exit from that is routine, not an AppleScript-level error
	-- to also report.
	try
		do shell script quoted form of binPath & " --uninstall"
	end try
end run
