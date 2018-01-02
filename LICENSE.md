## License ##

Jehanne inherits a lot of code from [Plan9][Plan9] through its [UC Berkeley release][brho],
[Plan9-9k][plan9-9k], [9front][9front] and [Harvey][harvey].

The initial [Plan 9][brho] code has been received, modified and
is redistributed according to the GNU General Public License, Version 2
as explained in /doc/licence/LICENSE.Plan9.txt.

The kernel is a direct fork of Charles Forsyth's [Plan9-9k][plan9-9k],
according to /doc/license/NOTICE.Plan9-9k.txt. Original kernel code is
released as GNU General Public License, Version 2.

New tools and improvements ported from 9front are subject to the MIT
license as explained in /doc/licence/9front-mit.txt.

New libraries and programs that use Plan9 libraries as system libraries
can be subject to different licenses, as documented in the respective
sources.

## New System Libraries ##

Some new libraries designed for use within Jehanne are released under
GNU Affero General Public License, Version 3.

They can be linked as "System Libraries" for use within Jehanne.

## Additional Licenses ##

Jehanne contains a number of third-party packages under various licenses.  
They are retrieved from their respective sources - licenses 
for these packages are not included here, but should be available 
from the respective directories in /sys/src, /pkgs or /hacking.

[Plan9]: https://9p.io/plan9/index.html
[brho]: https://github.com/brho/plan9
[harvey]: http://harvey-os.org "Harvey OS"
[9front]: http://9front.org/ "THE PLAN FELL OFF"
[plan9-9k]: https://bitbucket.org/forsyth/plan9-9k "Experimental 64-bit Plan 9 kernel"
