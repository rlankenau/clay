Import MaxGui.MaxGui
Import MaxGUI.Drivers



Include "gui.bmx"
Include "events.bmx"
Include "gadgetEvents.bmx"



Global window:TGadget = CreateWindow( "TEST", 0, 0, 640, 480, Null, WINDOW_TITLEBAR | WINDOW_MENU | WINDOW_CENTER | WINDOW_CLIENTCOORDS )
Global canvas:TGadget = CreateCanvas( 0, 0, 640, 480, window )
SetGraphics CanvasGraphics( canvas )

Global root:GadgetContainer = MakeGadgetContainer( 0, 0, 640, 480 )
Global _thing:Thing = MakeThing( 400, 200, 100, 200 ) 
_thing.AddChild( MakeThing( 10, 110, 200, 100 ) )
root.AddChild( _thing )



CreateTimer 30

While WaitEvent()
	HandleTEvent( root, CurrentEvent )
Wend





Function OnUpdate()
EndFunction



Function OnRender()
	SetColor 255, 255, 255
	SetOrigin 0, 0
	DrawText "x: " + _mx + " y: " + _my, 10, 10
	root.RenderExterior()
	root.RenderInterior()
EndFunction



Function MakeThing:Thing( x:Int, y:Int, w:Int, h:Int )
	Local thing:Thing = New Thing
	thing.x = x
	thing.y = y
	thing.w = w
	thing.h = h
	Return thing
EndFunction



Global sx:Int, sy:Int



Type Thing Extends GadgetContainer
	Method HandleEvent:Gadget( event:Event )
		Local child:Gadget = Super.HandleEvent( event )
		
		If child <> Self
			Return child
		EndIf
		
		Select event.id
		Case _EVENT_MOUSE_DOWN
			If event.data = MOUSE_RIGHT
				sx = x; sy = y
			Else
				sx = ox; sy = oy
			EndIf
		Case _EVENT_MOUSE_DRAG
			If event.data = MOUSE_RIGHT
				x = sx + event.dx; y = sy + event.dy
			Else
				ox = sx + event.dx; oy = sy + event.dy
			EndIf
		EndSelect
		
		Return Self
	EndMethod
	
	Method RenderExterior()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
	EndMethod
	
	Method RenderInterior()
		For Local x:Int = 1 To 5
		For Local y:Int = 1 To 5
			If x = 2 And y = 2
				DisableScissor()
			EndIf
			
			SetColor 255, 255, 255
			DrawRect x * 20, y * 20, 20, 20
			SetColor 0, 0, 0
			DrawRect x * 20 + 1, y * 20 + 1, 18, 18
			
			If x = 2 And y = 2
				EnableScissor()
			EndIf
		Next
		Next
		
		Super.RenderInterior()
	EndMethod
EndType