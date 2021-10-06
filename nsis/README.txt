This builds a one-click install for Vim for Win32 using the Nullsoft
Installation System (NSIS), available at http://nsis.sourceforge.net/

To build the installable .exe:

1.  Unpack three archives:
	PC sources
	PC runtime
	PC language files
    You can generate these from the Unix sources and runtime plus the extra
    archive (see the Makefile in the top directory).

2.  Go to the src directory and build:
	gvim.exe (the OLE version),
	vimrun.exe,
	install.exe,
	uninstal.exe,
	xxd/xxd.exe,

    Then execute tools/rename.bat to rename the executables. (mv command is
    required.)

3.  Go to the GvimExt directory and build gvimext.dll (or get it from a binary
    archive).  Both 64- and 32-bit versions are needed and should be placed
    as follows:
	64-bit: src/GvimExt/gvimext64.dll
	32-bit: src/GvimExt/gvimext.dll

4.  Go to the VisVim directory and build VisVim.dll (or get it from a binary
    archive).

5.  Go to the OleVim directory and build OpenWithVim.exe and SendToVim.exe (or
    get them from a binary archive).

6.  Get a "diff.exe" program and put it in the "../.." directory (above the
    "vim80" directory, it's the same for all Vim versions).
    You can find one in previous Vim versions or in this archive:
		http://www.mossbayeng.com/~ron/vim/diffutils.tar.gz
    Also put winpty32.dll and winpty-agent.exe there.

7.  Do "make uganda.nsis.txt" in runtime/doc.  This requires sed, you may have
    to do this on Unix.  Make sure the file is in DOS file format!

8.  Get gettext and iconv DLLs from the following site:
	https://github.com/mlocati/gettext-iconv-windows/releases
    Both 64- and 32-bit versions are needed.
    Download the files gettextX.X.X.X-iconvX.XX-shared-{32,64}.zip, extract
    DLLs and place them as follows:

	<GETTEXT directory>
	    |
	    + gettext32/
	    |	libintl-8.dll
	    |	libiconv-2.dll
	    |	libgcc_s_sjlj-1.dll
	    |
	    ` gettext64/
		libintl-8.dll
		libiconv-2.dll

    The default <GETTEXT directory> is "..", however, you can change it by
    passing /DGETTEXT=... option to the makensis command.


Install NSIS if you didn't do that already.
Also install UPX, if you want a compressed file.

To build then, enter:

	makensis gvim.nsi
