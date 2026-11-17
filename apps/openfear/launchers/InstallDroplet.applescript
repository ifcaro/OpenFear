-- Compiled into "Install OpenFear.app" via osacompile (cmake/OpenFearMacOS.cmake).
-- Drag a folder or .iso onto it to install from that source; a plain
-- double-click has nothing to auto-detect on macOS.
on run
	installFear("")
end run

on open droppedItems
	installFear(POSIX path of (item 1 of droppedItems))
end open

on installFear(sourcePath)
	-- Literal name, not "any *.app here that isn't me" - the latter would
	-- match an unrelated app if this ever sits in a folder with others in
	-- it (e.g. /Applications), launching garbage instead of failing clean.
	set myDir to do shell script "dirname " & quoted form of (POSIX path of (path to me))
	set binPath to myDir & "/OpenFear.app/Contents/MacOS/openfear"
	-- openfear itself already shows its own dialog for every outcome
	-- (success, no assets found, etc) via its own window - a non-zero
	-- exit from that is routine, not an AppleScript-level error to also
	-- report.
	try
		if sourcePath is "" then
			do shell script quoted form of binPath & " --install"
		else
			do shell script quoted form of binPath & " --assets " & quoted form of sourcePath & " --install"
		end if
	end try
end installFear
