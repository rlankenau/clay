Strict



Import main



Class Project Extends ContainerGadget
	Field path:String
	
	Field boxSelected:Box
	
	Field tray:Tray
	Field patch:Patch
	Field panel:Panel
	Field viewPanel:View
	
	Method New( path:String )
		x = 0
		y = TAB_HEIGHT + 4
		'TODO revisit
		w = 640
		h = 480
		
		APP.window.AddChild( Self )
		tray = New Tray( 0, 0, 640 - PANEL_WIDTH - 2, TRAY_HEIGHT )
		patch = New Patch( 0, TRAY_HEIGHT + 1, 640 - PANEL_WIDTH - 2, 480 - TRAY_HEIGHT - TAB_HEIGHT - 3 - 4 )
		
		panel = New Panel( 640 - PANEL_WIDTH - 1, 0, PANEL_WIDTH, 480 - TAB_HEIGHT - VIEW_HEIGHT - 3 - 4 )
		viewPanel = New View( panel.x, panel.y + panel.h + 1, panel.w, VIEW_HEIGHT )
		
		AddChild( patch )
		AddChild( tray )
		AddChild( panel )
		AddChild( viewPanel )
		
		Self.path = path
		
		If path <> ""
			'TODO
		EndIf
	End

	Method Update:Void()
		For Local spark:Spark = EachIn patch.sparks
			spark.Update()
		Next
		
		If KeyHit( KEY_ENTER )
			If boxSelected <> Null
				boxSelected.Execute
			EndIf
		EndIf
	
		If boxSelected <> Null
			Copy( viewPanel.box, boxSelected )
		Else
			Fill( viewPanel.box, 0 )
		EndIf
	End
End