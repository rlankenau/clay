Strict



Import gui



Const EVENT_SECRET:Int = -1
Const EVENT_MOUSE_MOVE:Int = 0
Const EVENT_MOUSE_DOWN_LEFT:Int = 1
Const EVENT_MOUSE_DOWN_RIGHT:Int = 2
Const EVENT_MOUSE_DRAG_LEFT:Int = 3
Const EVENT_MOUSE_DRAG_RIGHT:Int = 4
Const EVENT_MOUSE_UP_LEFT:Int = 5
Const EVENT_MOUSE_UP_RIGHT:Int = 6
Const EVENT_MOUSE_DOUBLE_CLICK_LEFT:Int = 7
Const EVENT_MOUSE_DOUBLE_CLICK_RIGHT:Int = 8
'TODO
Const EVENT_MOUSE_ENTER:Int = 5
Const EVENT_MOUSE_LEAVE:Int = 6



Global MOUSE_STATE_NONE:Int = 0
Global MOUSE_STATE_LEFT:Int = 1
Global MOUSE_STATE_RIGHT:Int = 2
Global MOUSE_STATE_BOTH_LEFT:Int = 3
Global MOUSE_STATE_BOTH_RIGHT:Int = 4




Global _mouseX:Int, _mouseY:int
Global _mousePreviousX:Int, _mousePreviousY:Int
Global _mouseDragX:Int, _mouseDragY:Int
Global _mouseState:Int
Global _mousePreviousState:int
Global _dominant:Int
Global _destination:Gadget



Class Event
	Field id:Int
	Field x:Int, y:Int
	Field dx:Int, dy:Int
	Field destination:Gadget
	
	Method New( id:Int )
		Self.id = id
		x = _mouseX; y = _mouseY
		
		Select id
			Case EVENT_MOUSE_DRAG_LEFT, EVENT_MOUSE_DRAG_RIGHT, EVENT_MOUSE_UP_LEFT, EVENT_MOUSE_UP_RIGHT
				dx = x - _mouseDragX; dy = y - _mouseDragY
				destination = _destination
			Default
		End
	End
End



Global _events := New List< Event >



Function HandleEvent:Void( root:ContainerGadget, event:Event )
	Select event.id
		Case EVENT_MOUSE_DOWN_LEFT, EVENT_MOUSE_DOWN_RIGHT
			_mouseDragX = _mouseX; _mouseDragY = _mouseY
			_destination = root.HandleEvent( event )
		Default
			root.HandleEvent( event )
	End
End



Function ProduceEvents:Void()
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