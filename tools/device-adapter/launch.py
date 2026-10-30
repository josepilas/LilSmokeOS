#!/usr/bin/env python3
import sys
if len(sys.argv) > 1:
    from adapter import main
    raise SystemExit(main())
else:
    from gui import main
    main()
