Strict



Import mojo
Import gui



Function Main:Int()
	New MyApp()
	Return 0
End



Class MyApp Extends App
	Global root:ContainerGadget = New ContainerGadget( 0, 0, 640, 480 )
	
	Method OnCreate:Int()
		SetUpdateRate( 30 )
		
		Local _thing:Thing = New Thing( 400, 200, 100, 200 )
		_thing.AddChild( New Thing( 10, 110, 200, 100 ) )
		root.AddChild( _thing )
		
		Return 0
	End
	
	Method OnUpdate:Int()
		ProduceEvents()
		
		While _events.Count() > 0
			HandleEvent( root, _events.RemoveFirst() )
		Wend
		
		Return 0
	End
	
	Method OnRender:Int()
		Cls()
		SetColor 255, 255, 255
		SetMatrix( 1, 0, 0, 1, 0, 0 )
		DrawText "x: " + _mouseX + " y: " + _mouseY, 10, 10
		root.Render()
		Return 0
	End
End



Global sx:Int, sy:Int



Class Thing Extends ContainerGadget
	Method New( x:Int, y:Int, w:Int, h:Int )
		Self.x = x; Self.y = y
		Self.w = w; Self.h = h
	End
	
	Method HandleEvent:Gadget( event:Event )
		Local child:Gadget = Super.HandleEvent( event )
		
		If child <> Self
			Return child
		EndIf
		
		Select event.id
		Case EVENT_MOUSE_DOWN_LEFT
			sx = ox; sy = oy
		Case EVENT_MOUSE_DOWN_RIGHT
			sx = x; sy = y
		Case EVENT_MOUSE_DRAG_LEFT
			ox = sx + event.dx; oy = sy + event.dy
		Case EVENT_MOUSE_DRAG_RIGHT
			x = sx + event.dx; y = sy + event.dy
		End
		
		Return Self
	End
	
	Method OnRender:Void()
		SetColor 255, 255, 255
		DrawRect 0, 0, w, h
		SetColor 0, 0, 0
		DrawRect 1, 1, w - 2, h - 2
	End
	
	Method OnRenderInterior:Void()
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
		
		Super.OnRenderInterior()
	End
End