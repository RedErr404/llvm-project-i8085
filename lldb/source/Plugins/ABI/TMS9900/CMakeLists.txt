add_lldb_library(lldbPluginABITMS9900 PLUGIN
  ABISysV_tms9900.cpp

  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
  LINK_COMPONENTS
    Support
    TargetParser
  )
