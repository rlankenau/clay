SuperStrict



'TODO
'settings
'boxes that take an any number of inputs ( darken, lighten, etc ) and expand so that there is always one free
'boxes that automatically rearrange their inputs if they are commutative and their lines cross
'sub patches
'drag-and-stretch a sub patch box around some boxes to make a new sub patch
'same with sequence, repeat, whatever
'error messages in status bar "cycle detected", "mismatched types", etc


Import MaxGui.MaxGui
Import MaxGUI.Drivers


Import BaH.Libxml



Include "box.bmx"
Include "wire.bmx"
Include "spark.bmx"
Include "functions.bmx"
Include "templates.bmx"
Include "gui.bmx"
Include "events.bmx"
Include "patch.bmx"
Include "tray.bmx"
Include "panel.bmx"
Include "settings.bmx"
Include "gadgets.bmx"
Include "gadgetevents.bmx"


SetMaskColor 0, 0, 0
Global imgO:TImage = LoadImage( "o.png", MASKEDIMAGE ); MidHandleImage imgO
Global imgX:TImage = LoadImage( "x.png", MASKEDIMAGE ); MidHandleImage imgX



Global window:TGadget = CreateWindow( "CLAY", 0, 0, 640, 480, Null, WINDOW_TITLEBAR | WINDOW_MENU | WINDOW_CENTER | WINDOW_CLIENTCOORDS | WINDOW_STATUS )
Global canvas:TGadget = CreateCanvas( 0, 0, 640, 480, window )
SetGraphics CanvasGraphics( canvas )

Global from:Box
Global sx:Int, sy:Int
Global _dragMode:Int



Const DRAG_NONE:Int = 0
Const DRAG_BOX:Int = 1
Const DRAG_WIRE:Int = 2
Const DRAG_PATCH:Int = 3



MakeTemplates()

Const TRAY_HEIGHT:Int = 30
Const PANEL_WIDTH:Int = 100
Const VIEW_HEIGHT:Int = 80

Global root:GadgetContainer = MakeGadgetContainer( 0, 0, 640, 480 )
Global patch:TPatch = TPatch.Make( 1, TRAY_HEIGHT + 2, 640 - PANEL_WIDTH - 3, 480 - TRAY_HEIGHT - 3 )
root.AddChild( patch )
Global panel:TPanel = TPanel.Make( 640 - PANEL_WIDTH - 1, TRAY_HEIGHT + 2, PANEL_WIDTH, 480 - TRAY_HEIGHT - VIEW_HEIGHT - 4 )
root.AddChild( panel )
Global tray:TTray = TTray.Make( 1, 1, 640 - 2, TRAY_HEIGHT )
root.AddChild( tray )
Global viewPanel:TView = TView.Make( panel.x, 480 - VIEW_HEIGHT - 1, panel.w, VIEW_HEIGHT )
root.AddChild( viewPanel )

'panel.AddChild( BoolProperty.Make( 4, 4, "wrap", True ) )

'panel.AddChild( FloatProperty.Make( 4, 24, "density", 0.5 ) )

'panel.AddChild( NumberBox.Make( 4, 4, 2, 1, 9 ) )

CreateTimer 30

While WaitEvent()
	HandleTEvent( root, CurrentEvent )
Wend



Function OnUpdate()
	For Local spark:Spark = EachIn patch.sparks
		spark.Update()
	Next
	
	If boxSelected <> Null
		View( viewPanel.box, boxSelected )
	Else
		_Clear( viewPanel.box )
	EndIf
EndFunction



Function OnRender()
	root.RenderInterior()
EndFunction



Function MakeSparks( box:Box )
	For Local wire:Wire = EachIn patch.wires
		If wire.a = box
			patch.sparks.AddLast( Spark.Make( wire ) )
		EndIf
	Next
EndFunction




Function DeleteBox( box:Box )
	If box = Null
		Return
	EndIf
	
	'TODO quit playback
	
	For Local wire:Wire = EachIn patch.wires
		If wire.a = box Or wire.b = box
			DeleteWire( wire )
		EndIf
	Next
	
	patch.boxes.Remove( box )
EndFunction



Function DeleteWire( wire:Wire )
	If wire = Null
		Return
	EndIf
	
	For Local spark:Spark = EachIn patch.sparks
		If spark.wire = wire
			patch.sparks.Remove( spark )
		EndIf
	Next
	
	patch.wires.Remove( wire )
EndFunction



Function CycleCheck:Int( a:Box, b:Box )
	Local list:TList = New TList
	list.AddLast( b )
	Local count:Int = 0
	
	While list.Count() <> count
		count = list.Count()
		
		For Local wire:Wire = EachIn patch.wires
			If list.Contains( wire.b )
				If Not list.Contains( wire.a )
					list.AddLast( wire.a )
				EndIf
			EndIf
		Next
	EndWhile
	
	If list.Contains( a )
		Return True
	EndIf
	
	Return False
EndFunction