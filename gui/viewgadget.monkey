Strict



Import gui



Class ViewGadget Extends Gadget
	Field ox:Int, oy:Int, scissor:Bool = True
	
	Method xTranslate:Int() Property
		Return x + ox
	End
	
	Method yTranslate:Int() Property
		Return y + oy
	End
	
	Method Render:Void()
		OnRender()
		PushMatrix()
		Translate ox, oy
		OnRenderInterior()
		PopMatrix()
	End
	
	Method OnRenderInterior:Void()
		
	End
End