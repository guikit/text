/*
 * Minimal FreeType module registry for guikit::text.
 * Retains modern vector UI font formats (TTF / TTC / OTF CFF) + auto-hinter + smooth/LCD rasterizer.
 * Excludes legacy 1980s-90s formats (Type1, CID, Type42, WinFNT, BDF, PCF, PFR) and unused rasterizers
 * (1-bit monochrome raster1, CPU SDF, OT-SVG, internal ftcache, gzip/lzw/bzip2).
 */

FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )

/* EOF */
