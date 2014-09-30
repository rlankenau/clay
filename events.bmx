Const _EVENT_SECRET:Int = -1
Const _EVENT_MOUSE_MOVE:Int = 0
Const _EVENT_MOUSE_DOWN:Int = 1
Const _EVENT_MOUSE_DRAG:Int = 2
Const _EVENT_MOUSE_UP:Int = 3
Const _EVENT_MOUSE_DOUBLE_CLICK:Int = 4
'TODO
Const _EVENT_MOUSE_ENTER:Int = 5
Const _EVENT_MOUSE_LEAVE:Int = 6



Global _mouseDown:Int
Global _mouseState:Int[2]
Global _mPrevious:Int
Global _mx:Int, _my:Int 'mouse coordinates
Global _px:Int, _py:Int 'previous mouse coordinates from before a drag event started
Global _destination:Gadget



Type Event
	Field id:Int, data:Int
	Field x:Int, y:Int
	Field dx:Int, dy:Int
	Field destination:Gadget
EndType



Function MakeSecretEvent:Event()
	Local event:Event = New Event
	event.id = _EVENT_SECRET
	event.x = _mx; event.y = _my
	Return event
EndFunction



Function MakeMouseMoveEvent:Event()
	Local event:Event = New Event
	event.id = _EVENT_MOUSE_MOVE
	event.x = _mx; event.y = _my
	Return event
EndFunction



Function MakeMouseDragEvent:Event( data:Int, destination:Gadget )
	Local event:Event = New Event
	event.id = _EVENT_MOUSE_DRAG
	event.data = data
	event.x = _mx; event.y = _my
	event.dx = event.x - _px; event.dy = event.y - _py
	event.destination = destination
	Return event
EndFunction



Function MakeMouseDownEvent:Event( data:Int )	
	Local event:Event = New Event
	event.id = _EVENT_MOUSE_DOWN
	event.data = data
	event.x = _mx; event.y = _my
	Return event
EndFunction



Function MakeMouseUpEvent:Event( data:Int, destination:Gadget )
	Local event:Event = New Event
	event.id = _EVENT_MOUSE_UP
	event.data = data
	event.x = _mx; event.y = _my
	event.destination = destination
	Return event
EndFunction



Function MakeMouseDoubleClickEvent:Event( data:Int )
	Local event:Event = New Event
	event.id = _EVENT_MOUSE_DOUBLE_CLICK
	event.data = data
	event.x = _mx; event.y = _my
	Return event
EndFunction



Function HandleTEvent( root:GadgetContainer, _tEvent:TEvent )
	Select _tEvent.id
	Case EVENT_MOUSEMOVE
		_mx = _tEvent.x; _my = _tEvent.y
		
		If _mouseDown <> 0
			root.HandleEvent( MakeMouseDragEvent( _mouseDown, _destination ) )
		Else
			root.HandleEvent( MakeMouseMoveEvent() )
		EndIf
	Case EVENT_KEYDOWN
		Select EventData()
		'Case KEY_DELETE, KEY_BACKSPACE
		'	DeleteBox( patch.boxSelected )
		EndSelect
	Case EVENT_MOUSEDOWN
		_mouseDown = _tEvent.data
		_mouseState[ _tEvent.data - 1 ] = 1
		_mx = _tEvent.x; _my = _tEvent.y
		_px = _mx; _py = _my
		Local other:Int = 1 - ( _tEvent.data - 1 )
		
		If _mouseState[ other ] = 1
			root.HandleEvent( MakeMouseUpEvent( other + 1, _destination) )
			_mouseState[ other ] = 0
		EndIf
		
		If MilliSecs() - _mPrevious < 500
			_destination = root.HandleEvent( MakeMouseDoubleClickEvent( _tEvent.data ) )
			_mPrevious = 0
		Else
			_destination = root.HandleEvent( MakeMouseDownEvent( _tEvent.data ) )
		EndIf
		
		_mPrevious = MilliSecs()
	Case EVENT_MOUSEUP
		_mouseDown = 0
		_mx = _tEvent.x; _my = _tEvent.y
		
		If _mouseState[ _tEvent.data - 1 ] = 1
			root.HandleEvent( MakeMouseUpEvent( _tEvent.data, _destination ) )
			_mouseState[ _tEvent.data - 1 ] = 0
		EndIf
		
		_dragMode = DRAG_NONE
	Case EVENT_TIMERTICK
		OnUpdate()
		RedrawGadget canvas
	
	Case EVENT_GADGETPAINT
		SetGraphics CanvasGraphics( canvas )
		SetBlend MASKBLEND
		SetColor 255, 255, 255
		SetViewport 0, 0, 640, 480
		SetOrigin 0, 0
		OnRender()
		Flip
		Cls
	
	Case EVENT_WINDOWCLOSE
		FreeGadget canvas
		End
	
	Case EVENT_APPTERMINATE
		End
	EndSelect
EndFunction
