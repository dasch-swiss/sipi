# ICC profile creation date is deterministic in tests, wall-clock in production

`SipiIcc::iccBytes()` is the single chokepoint that converts `cmsHPROFILE` to bytes for codec consumption. When `SOURCE_DATE_EPOCH` is set in the environment, it overwrites bytes 24-35 (ICC creation date) with the supplied epoch and zeros bytes 84-99 (Profile ID). When unset — the production default — it returns lcms2's wall-clock-stamped bytes verbatim.

We accept this split because byte-for-byte approval testing of JPEG / PNG / JP2 outputs is otherwise impossible (lcms2's `cmsCreateProfilePlaceholder` calls `time(NULL)` and lcms2 upstream rejected `SOURCE_DATE_EPOCH` support — Debian #814883, Little-CMS #71). Production keeps wall-clock timestamps so downstream consumers reading `cmsGetHeaderCreationDateTime` see real creation times, matching every other IIIF server.

The alternatives — patching lcms2 (fork burden), stripping ICC profiles from outputs (loses colour management), or pixel-tolerance / SSIM-based testing (looser gate, hides real encoder drift) — were rejected because the chokepoint design makes the test-only fix one function call.
