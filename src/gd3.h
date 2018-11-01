#ifndef GD3_H
#define GD3_H

#ifdef CPLUSPLUS
extern "C" {
#endif

#define GD3IDENT "Gd3 "
#define GD3IDENT_LEN 4

	typedef struct 
	{
		char GD3Ident[4];      // "Gd3 "
		unsigned long Version; // 0x000000100 for 1.00
		unsigned long Length;  // Length of string data following this point
	} GD3Header;

	enum {
		GD3X_DUMMYBASEVALUE = -1000, // make sure we have values below 0
		GD3X_TRACKNO,
		GD3X_TRACKGAIN,
		GD3X_TRACKPEAK,
		GD3X_ALBUMGAIN,
		GD3X_ALBUMPEAK,
		GD3X_ALBUMARTIST,
		GD3X_PUBLISHER,
		// above here don't need to read the file
		GD3X_UNKNOWN,
		// below here need the VGM header
		GD3X_LENGTH,
		// below here need the GD3 tag, first must be 0
		GD3_TITLEEN = 0, GD3_TITLEJP,
		GD3_GAMEEN  , GD3_GAMEJP,
		GD3_SYSTEMEN, GD3_SYSTEMJP,
		GD3_AUTHOREN, GD3_AUTHORJP,
		GD3_DATE,
		GD3_RIPPER,
		GD3_NOTES,
		NUMGD3TAGS // auto-set to the right number
	};

#ifdef CPLUSPLUS
}
#endif

#endif
