Strict



Import main



Class WindowGadget Extends ContainerGadget
	Field _mouseX:Int, _mouseY:int
	Field _mousePreviousX:Int, _mousePreviousY:Int
	Field _mouseDragX:Int, _mouseDragY:Int
	Field _mouseState:Int
	Field _mousePreviousState:Int
	Field _destination:Gadget
	Field _events := New List< Event >
	
	Method New( x:Int, y:Int, w:Int, h:Int )
		Super.New( x, y, w, h )
	End
	
	Method AddChild:Void( child:Gadget )
		children.AddLast( child )
		child.parent = Self
		child.window = Self
	End
	
	Method HandleEvent:Gadget( event:Event )
		Local destination:Gadget = Super.HandleEvent( event )
		Select event.id
			Case EVENT_MOUSE_DOWN_LEFT, EVENT_MOUSE_DOWN_RIGHT
				_mouseDragX = _mouseX; _mouseDragY = _mouseY
				_destination = destination
			Default
		End
		
		Return destination
	End
	
	Method Update:Void()
		_mousePreviousX = _mouseX
		_mousePreviousY = _mouseY
	
		_mouseX = MouseX()
		_mouseY = MouseY()
		
		If _mouseX <> _mousePreviousX Or _mouseY <> _mousePreviousY
			Local state:Int = _mouseState
			If state >= 3 Then state -= 2
			_events.AddLast( New Event( [ EVENT_MOUSE_MOVE, EVENT_MOUSE_DRAG_LEFT, EVENT_MOUSE_DRAG_RIGHT ][ state ] ) )
		EndIf
		
		_mousePreviousState = _mouseState
		
		If MouseDown( MOUSE_LEFT ) And MouseDown( MOUSE_RIGHT )
			Select _mousePreviousState
				Case MOUSE_STATE_NONE, MOUSE_STATE_RIGHT, MOUSE_STATE_BOTH_LEFT, MOUSE_STATE_BOTH_RIGHT
					_mouseState = MOUSE_STATE_BOTH_LEFT
				Case MOUSE_STATE_LEFT
					_mouseState = MOUSE_STATE_BOTH_RIGHT
				Default
			End
		ElseIf MouseDown( MOUSE_LEFT )
			_mouseState = MOUSE_STATE_LEFT
		ElseIf MouseDown( MOUSE_RIGHT )
			_mouseState = MOUSE_STATE_RIGHT
		Else
			_mouseState = MOUSE_STATE_NONE
		EndIf
		
		Local id:Int[] = [ -2, -2 ]
		
		Select _mouseState
			Case MOUSE_STATE_NONE
				Select _mousePreviousState
					Case MOUSE_STATE_NONE
						'do nothing
					Case MOUSE_STATE_LEFT, MOUSE_STATE_BOTH_LEFT
						id[0] = EVENT_MOUSE_UP_LEFT
					Case MOUSE_STATE_RIGHT, MOUSE_STATE_BOTH_RIGHT
						id[0] = EVENT_MOUSE_UP_RIGHT
					Default
				End
			Case MOUSE_STATE_LEFT, MOUSE_STATE_BOTH_LEFT
				Select _mousePreviousState
					Case MOUSE_STATE_NONE
						id[0] = EVENT_MOUSE_DOWN_LEFT
					Case MOUSE_STATE_LEFT, MOUSE_STATE_BOTH_LEFT
						'do nothing
					Case MOUSE_STATE_RIGHT, MOUSE_STATE_BOTH_RIGHT
						id[0] = EVENT_MOUSE_UP_RIGHT
						id[1] = EVENT_MOUSE_DOWN_LEFT
					Default
				End
			Case MOUSE_STATE_RIGHT, MOUSE_STATE_BOTH_RIGHT
				Select _mousePreviousState
					Case MOUSE_STATE_NONE
						id[0] = EVENT_MOUSE_DOWN_RIGHT
					Case MOUSE_STATE_LEFT, MOUSE_STATE_BOTH_LEFT
						id[0] = EVENT_MOUSE_UP_LEFT
						id[1] = EVENT_MOUSE_DOWN_RIGHT
					Case MOUSE_STATE_RIGHT, MOUSE_STATE_BOTH_RIGHT
						'do nothing
					Default
				End
			Default
		End
		
		For Local n:Int = 0 To 1
			If id[n] <> -2
				_events.AddLast( New Event( id[n] ) )
			EndIf
		Next
	End
End