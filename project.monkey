Strict



Import main



Class Project Extends ContainerGadget
	Field path:String
	Field saved:Bool = False
	
	Field boxSelected:Box
	
	Field tray:Tray
	Field patch:Patch
	Field panel:SidePanel
	Field viewSidePanel:View
	
	Method New( path:String )
		x = 0
		y = TAB_HEIGHT + 4
		'TODO revisit
		w = 640
		h = 480
		
		'UGH
		Event.globalWindow.AddChild( Self )
		tray = New Tray( 0, 0, 640 - PANEL_WIDTH - 2, TRAY_HEIGHT )
		patch = New Patch( 0, TRAY_HEIGHT + 1, 640 - PANEL_WIDTH - 2, 480 - TRAY_HEIGHT - TAB_HEIGHT - 3 - 4 )
		
		panel = New SidePanel( 640 - PANEL_WIDTH - 1, 0, PANEL_WIDTH, 480 - TAB_HEIGHT - VIEW_HEIGHT - 3 - 4 )
		viewSidePanel = New View( panel.x, panel.y + panel.h + 1, panel.w, VIEW_HEIGHT )
		
		AddChild( patch )
		AddChild( tray )
		AddChild( panel )
		AddChild( viewSidePanel )
		
		Self.path = path
		
		If path <> ""
			Load()
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
			Copy( viewSidePanel.box, boxSelected )
		Else
			Fill( viewSidePanel.box, 0 )
		EndIf
	End
	
	Method Load:Void()
		_Load( os.LoadString( path ) )
	End
	
	Method _LoadInternal:Void()
		_Load( app.LoadString( path ) )
	End
	
	Method _Load:Void( data:String )
		Local doc:XMLDocument = New XMLParser().ParseString( data )
		'''Box.idNext = Int( doc.Root.GetAttribute( "idNext" ) )
		patch.ox = Int( doc.Root.GetAttribute( "ox" ) )
		patch.oy = Int( doc.Root.GetAttribute( "oy" ) )
		Local boxes:XMLElement = doc.Root.GetFirstChildByName( "boxes" )
		
		For Local child:XMLElement = EachIn boxes.Children
			Local x:Int = Int( child.GetAttribute( "x" ) )
			Local y:Int = Int( child.GetAttribute( "y" ) )
			Local template:Template = _GetTemplate( child.Name )
			Local box:Box = New Box( x, y, template )
			box.id = Int( child.GetAttribute( "id" ) )
			patch.boxes.AddLast( box )
			Local setting:XMLElement = child.GetFirstChildByName( "settings" )
			
			For Local attribute:XMLAttribute = EachIn setting.Attributes
				box.settings.Get( attribute.Name ).value = Int( attribute.Value )
			Next
		Next
		
		Local wires:XMLElement = doc.Root.GetFirstChildByName( "wires" )
		
		For Local child:XMLElement = EachIn wires.Children
			Local a:Box = patch._GetBoxById( Int( child.GetAttribute( "from" ) ) )
			Local b:Box = patch._GetBoxById( Int( child.GetAttribute( "to" ) ) )
			Local bId:Int = Int( child.GetAttribute( "toId" ) )
			patch.wires.AddLast( New Wire( a, b, bId ) )
		Next
	End
	
	Method Save:Void()
		If path = "" Then path = SaveFileName()
		If path = "" Then Return
		saved = True
		
		Local root:XMLElement = New XMLElement()
		root.Name = "root"
		root.SetAttribute( "idNext", Box.idNext )
		root.SetAttribute( "ox", patch.ox )
		root.SetAttribute( "oy", patch.oy )
		Local doc:XMLDocument = New XMLDocument( root )
		Local boxes := New XMLElement( "boxes", root )
		
		For Local box:Box = EachIn patch.boxes
			Local element := New XMLElement( box.kind, boxes )
			element.SetAttribute( "id", box.id )
			element.SetAttribute( "x", box.x )
			element.SetAttribute( "y", box.y )
			Local settings := New XMLElement( "settings", element )
			
			For Local setting:Setting = EachIn box.settings.Values()
				settings.SetAttribute( setting.name, setting.value )
			Next
		Next
		
		Local wires := New XMLElement( "wires", root )
		
		For Local wire:Wire = EachIn patch.wires
			Local element := New XMLElement( "wire", wires )
			element.SetAttribute( "from", wire.a.id )
			element.SetAttribute( "to", wire.b.id )
			element.SetAttribute( "toId", wire.bId )
		Next
		
		SaveString( doc.ExportString(), path )
	End
End