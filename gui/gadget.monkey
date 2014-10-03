Strict



Import gui



Class Gadget
	Field window:WindowGadget
	Field parent:ContainerGadget
	Field x:Int, y:Int, w:Int, h:Int
	Field _enabled:Bool = True
	
	Method enabled:Bool() Property; Return _enabled; End	
	Method Enable:Void(); _enabled = True; End
	Method Disable:Void(); _enabled = False; End
	
	Method xTranslate:Int() Property; Return x; End
	Method yTranslate:Int() Property; Return y; End
	
	Method HandleEvent:Gadget( event:Event ) Abstract
	
	Method HandleGadgetEvent:Void( event:GadgetEvent )
		If parent <> Null
			parent.HandleGadgetEvent( event )
		EndIf
	End
	
	Method Render:Void()
		OnRender()
	End
	
	Method OnRender:Void()
		
	End
	
	'TODO
	Method _LocalX:Int( _x:Int )
		If parent <> Null Then _x = parent._LocalX( _x )
		Return _x - xTranslate
	End
	
	Method _LocalY:Int( _y:Int )
		If parent <> Null Then _y = parent._LocalY( _y )
		Return _y - yTranslate
	End
	
	Method _GlobalX:Int( _x:Int )
		_x = _x + xTranslate
		If parent <> Null Then _x = parent._GlobalX( _x )
		Return _x
	End
	
	Method _GlobalY:Int( _y:Int )
		_y = _y + yTranslate
		If parent <> Null Then _y = parent._GlobalY( _y )
		Return _y
	End
	
	Method GetLocalX:Int( x:Int, from:Gadget = window )
		Return Self._LocalX( from._GlobalX( x ) )
	End
	
	Method GetLocalY:Int( y:Int, from:Gadget = window )
		Return Self._LocalY( from._GlobalY( y ) )
	End
End