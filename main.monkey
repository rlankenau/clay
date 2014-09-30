Strict



'TODO
'abstract HandleEvent out of Gadget into IGadgetHandleEvent, and also into HandleMouseEvent, HandleKeyboardEvent etc
'settings
'boxes that take an any number of inputs ( darken, lighten, etc ) and expand so that there is always one free
'boxes that automatically rearrange their inputs if they are commutative and their lines cross
'sub patches
'drag-and-stretch a sub patch box around some boxes to make a new sub patch
'same with sequence, repeat, whatever
'error messages in status bar "cycle detected", "mismatched types", etc



#MOJO_IMAGE_FILTERING_ENABLED = False

Import mojo
Import gui



Import box
Import spark
'''Include "functions.bmx"
Import template
'''Include "gui.bmx"
'''Include "events.bmx"
Import patch
Import tray
'''Include "panel.bmx"
'''Include "settings.bmx"
'''Include "gadgets.bmx"
'''Include "gadgetevents.bmx"
Import wire



Global imgO:Image, imgX:Image



Global from:Box
Global sx:Int, sy:Int
Global _dragMode:Int



Const DRAG_NONE:Int = 0
Const DRAG_BOX:Int = 1
Const DRAG_WIRE:Int = 2



Const TRAY_HEIGHT:Int = 30
Const PANEL_WIDTH:Int = 100
Const VIEW_HEIGHT:Int = 80



Function Main:Int()
	APP = New MyApp()
	Return 0
End



Global APP:MyApp



Class MyApp Extends App
	Global root:ContainerGadget
	Global patch:Patch
	'''Global panel:Panel
	Global tray:Tray
	'''Global viewPanel:View
	
	Method OnCreate:Int()
		SetUpdateRate( 30 )
		
		imgX = LoadImage( "x.png" , 1, Image.MidHandle )
		imgO = LoadImage( "o.png" , 1, Image.MidHandle )
		
		MakeTemplates()
		
		root = New ContainerGadget( 0, 0, 640, 480 )
		patch = New Patch( 1, TRAY_HEIGHT + 2, 640 - PANEL_WIDTH - 3, 480 - TRAY_HEIGHT - 3 )
		'''panel = New Panel( 640 - PANEL_WIDTH - 1, TRAY_HEIGHT + 2, PANEL_WIDTH, 480 - TRAY_HEIGHT - VIEW_HEIGHT - 4 )
		tray = New Tray( 1, 1, 640 - 2, TRAY_HEIGHT )
		'''viewPanel = New View( panel.x, 480 - VIEW_HEIGHT - 1, panel.w, VIEW_HEIGHT )
		
		root.AddChild( patch )
		'''root.AddChild( panel )
		root.AddChild( tray )
		'''root.AddChild( viewPanel )
		Return 0
	End
	
	Method OnUpdate:Int()
		ProduceEvents()
		
		While _events.Count() > 0
			HandleEvent( root, _events.RemoveFirst() )
		Wend
		
		If KeyHit( KEY_B )
			patch.boxes.AddLast( New Box( patch.LocalX( _mouseX ), patch.LocalY( _mouseY ), New Template( "dum", 2, 1 ) ) )
		EndIf
		
		For Local spark:Spark = EachIn patch.sparks
			spark.Update()
		Next
	
		If boxSelected <> Null
			'''View( viewPanel.box, boxSelected )
		Else
			'''_Clear( viewPanel.box )
		EndIf
		
		Return 0
	End
	
	Method OnRender:Int()
		Cls
		SetMatrix 1, 0, 0, 1, 0, 0
		root.Render()
		Return 0
	End
End



Function MakeSparks:Void( box:Box )
	For Local wire:Wire = EachIn APP.patch.wires
		If wire.a = box
			APP.patch.sparks.AddLast( New Spark( wire ) )
		EndIf
	Next
End



Function DeleteBox:Void( box:Box )
	If box = Null Then Return
	
	'TODO quit playback
	
	For Local wire:Wire = EachIn APP.patch.wires
		If wire.a = box Or wire.b = box
			DeleteWire( wire )
		EndIf
	Next
	
	APP.patch.boxes.Remove( box )
End



Function DeleteWire:Void( wire:Wire )
	If wire = Null Then Return
	
	For Local spark:Spark = EachIn APP.patch.sparks
		If spark.wire = wire Then APP.patch.sparks.RemoveEach( spark )
	Next
	
	APP.patch.wires.Remove( wire )
End



Function CycleCheck:Bool( a:Box, b:Box )
	Local list := New List< Box >()
	list.AddLast( b )
	Local count:Int = 0
	
	While list.Count() <> count
		count = list.Count()
		
		For Local wire:Wire = EachIn APP.patch.wires
			If list.Contains( wire.b )
				If Not list.Contains( wire.a )
					list.AddLast( wire.a )
				EndIf
			EndIf
		Next
	Wend
	
	If list.Contains( a )
		Return True
	EndIf
	
	Return False
End