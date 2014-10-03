Strict



'TODO
'BOX SELECT DUMMY
'actual doubleclick
'abstract HandleEvent out of Gadget into IGadgetHandleEvent, and also into HandleMouseEvent, HandleKeyboardEvent etc
'settings
'boxes that take an any number of inputs ( darken, lighten, etc ) and expand so that there is always one free
'boxes that automatically rearrange their inputs if they are commutative and their lines cross
'sub patches
'drag-and-stretch a sub patch box around some boxes to make a new sub patch
'same with sequence, repeat, whatever
'error messages in status bar "cycle detected", "mismatched types", etc
'editable bindings for boxes, i.e. assign 'q' to one generator, 'w' to another, 'enter' to the 'go' prototype
'allow laps to go to zero for automata( bypass )
'have bypass checkbox automatically on all boxes that have one input and one output of the same type
' patches can have "slots" where you can put boxes that match the template
' "share" parameter for automata ( and other stuff? ) no this is redundant when parameter inlets exist, well maybe not
' one option would be "unique" ( don't share )
'mouse over inlets, outlets to see hover text describing the parameter name and type



#GLFW_USE_MINGW = False
#MOJO_IMAGE_FILTERING_ENABLED = False



Import mojo
Import os
Import gui
Import ninepatch
Import diddy.externfunctions
Import diddy.xml



Import box
Import browser
Import spark
Import functions
Import template
Import patch
Import tray
Import panel
Import setting
Import gadgets
Import wire
Import project



Global imgO:Image, imgX:Image, imgTab:NinePatchImage, imgClose:Image, imgOpen:Image, imgSave:Image, imgNew:Image



Global from:Box
Global sx:Int, sy:Int
Global _dragMode:Int



Const DRAG_NONE:Int = 0
Const DRAG_BOX:Int = 1
Const DRAG_WIRE:Int = 2



Const TRAY_HEIGHT:Int = 30
Const PANEL_WIDTH:Int = 100
Const VIEW_HEIGHT:Int = 80
Const TAB_HEIGHT:Int = 18



Function Main:Int()
	APP = New MyApp()
	Return 0
End



Global APP:MyApp



Class MyApp Extends App
	Field window:WindowGadget
	Field browser:Browser
	Field _project:Project
	Method project:Project() Property; Return _project; End
	
	Method project:Void( value:Project ) Property
		If _project <> Null Then _project.Disable()
		_project = value
		If _project <> Null Then _project.Enable()
	End
	
	Method patch:Patch() Property; Return project.patch; End
	Method panel:Panel() Property; Return project.panel; End
	Method tray:Tray() Property; Return project.tray; End
	Method viewPanel:View() Property; Return project.viewPanel; End
	
	Method OnCreate:Int()
		SetUpdateRate( 30 )
		
		imgX = LoadImage( "x.png" , 1, Image.MidHandle )
		imgX.SetHandle( imgX.HandleX() - 0.5, imgX.HandleY() - 0.5 )
		imgO = LoadImage( "o.png" , 1, Image.MidHandle )
		imgO.SetHandle( imgO.HandleX() - 0.5, imgO.HandleY() - 0.5 )
		imgTab = New NinePatchImage( LoadImage( "tab.png" ), [ 9, 8, 1, 1 ] ) '1, 1 is a klude, TODO 3 patch
		imgClose = LoadImage( "close.png" )
		imgOpen = LoadImage( "open.png" )
		imgSave = LoadImage( "save.png" )
		imgNew = LoadImage( "new.png" )
		SetFont( LoadImage( "font.png", 96, Image.XPadding ) )
		
		MakeTemplates()
		
		window = New WindowGadget( 0, 0, 640, 480 )
		Event.globalWindow = window
		browser = New Browser( 0, 1, 640, TAB_HEIGHT )
		window.AddChild( browser )
		
		Return 0
	End
	
	Method OnUpdate:Int()
		window.Update()
		
		While window._events.Count() > 0
			window.HandleEvent( window._events.RemoveFirst() )
		Wend
		
		If _project <> Null Then _project.Update()
		
		Return 0
	End
	
	Method OnRender:Int()
		Cls
		SetMatrix 1, 0, 0, 1, 0, 0
		window.Render()
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